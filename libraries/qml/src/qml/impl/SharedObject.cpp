//
//  Created by Bradley Austin Davis on 2018-01-04
//  Copyright 2013-2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "SharedObject.h"

#include <QtCore/qlogging.h>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <QtQml/QQmlContext>
#include <QtQml/QQmlEngine>

#include <QtGui/QOpenGLContext>

#include <NumericalConstants.h>
#include <shared/NsightHelpers.h>
#include <gl/QOpenGLContextWrapper.h>

#include "../OffscreenSurface.h"
#include "../Logging.h"

#include "Profiling.h"
#include "RenderControl.h"
#include "RenderEventHandler.h"
#include "TextureCache.h"

// Time between receiving a request to render the offscreen UI actually triggering
// the render.  Could possibly be increased depending on the framerate we expect to
// achieve.
// This has the effect of capping the framerate at 200
static const int MIN_TIMER_MS = 5;

using namespace hifi::qml;
using namespace hifi::qml::impl;

TextureCache offscreenTextures;

TextureCache& SharedObject::getTextureCache() {
    return offscreenTextures;
}

#define OFFSCREEN_QML_SHARED_CONTEXT_PROPERTY "com.highfidelity.qml.gl.sharedContext"
void SharedObject::setSharedContext(QOpenGLContext* sharedContext) {
    qApp->setProperty(OFFSCREEN_QML_SHARED_CONTEXT_PROPERTY, QVariant::fromValue<void*>(sharedContext));
    if (QOpenGLContextWrapper::currentContext() != sharedContext) {
        qFatal("The shared context must be the current context when setting");
    }
}

QOpenGLContext* SharedObject::getSharedContext() {
    return static_cast<QOpenGLContext*>(qApp->property(OFFSCREEN_QML_SHARED_CONTEXT_PROPERTY).value<void*>());
}

SharedObject::SharedObject() {
    // Create render control
    _renderControl = new RenderControl();

    // Create a QQuickWindow that is associated with our render control.
    // This window never gets created or shown, meaning that it will never get an underlying native (platform) window.
    // NOTE: Must be created on the main thread so that OffscreenQmlSurface can send it events
    // NOTE: Must be created on the rendering thread or it will refuse to render,
    //       so we wait until after its ctor to move object/context to this thread.
    QQuickWindow::setDefaultAlphaBuffer(true);
    _quickWindow = new QQuickWindow(_renderControl);
    _quickWindow->setColor(QColor(255, 255, 255, 0));
    _quickWindow->setClearBeforeRendering(true);


    QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, &SharedObject::onAboutToQuit);
}

SharedObject::~SharedObject() {
    if (_quickWindow) {
        _quickWindow->destroy();
        _quickWindow = nullptr;
    }

    if (_renderControl) {
        _renderControl->deleteLater();
        _renderControl = nullptr;
    }

    if (_renderThread) {
        _renderThread->quit();
        _renderThread->deleteLater();
    }

    if (_rootItem) {
        _rootItem->deleteLater();
        _rootItem = nullptr;
    }

    releaseEngine(_qmlContext->engine());
}

void SharedObject::create(OffscreenSurface* surface) {
    if (_rootItem) {
        qFatal("QML surface root item already set");
    }

    QObject::connect(_quickWindow, &QQuickWindow::focusObjectChanged, surface, &OffscreenSurface::onFocusObjectChanged);

    // Create a QML engine.
    auto qmlEngine = acquireEngine(surface);
    _qmlContext = new QQmlContext(qmlEngine->rootContext(), qmlEngine);
    surface->onRootContextCreated(_qmlContext);
    emit surface->rootContextCreated(_qmlContext);

    if (!qmlEngine->incubationController()) {
        qmlEngine->setIncubationController(_quickWindow->incubationController());
    }
    _qmlContext->setContextProperty("offscreenWindow", QVariant::fromValue(_quickWindow));
}

void SharedObject::setRootItem(QQuickItem* rootItem) {
    _rootItem = rootItem;
    _rootItem->setSize(_quickWindow->size());

    // Create the render thread
    _renderThread = new QThread();
    _renderThread->setObjectName(objectName());
    _renderThread->start();


    // Create event handler for the render thread
    _renderObject = new RenderEventHandler(this, _renderThread);
    QCoreApplication::postEvent(this, new OffscreenEvent(OffscreenEvent::Initialize));

    QObject::connect(_renderControl, &QQuickRenderControl::renderRequested, this, &SharedObject::requestRender);
    QObject::connect(_renderControl, &QQuickRenderControl::sceneChanged, this, &SharedObject::requestRenderSync);
}

void SharedObject::destroy() {
    if (_quit) {
        return;
    }

    if (!_rootItem) {
        deleteLater();
        return;
    }


    _paused = true;
    if (_renderTimer) {
        QObject::disconnect(_renderTimer);
        _renderTimer->deleteLater();
    }

    QObject::disconnect(_renderControl);
    QObject::disconnect(qApp);

    {
        QMutexLocker lock(&_mutex);
        _quit = true;
        QCoreApplication::postEvent(_renderObject, new OffscreenEvent(OffscreenEvent::Quit), Qt::HighEventPriority);
    }
    // Block until the rendering thread has stopped
    // FIXME this is undesirable because this is blocking the main thread,
    // but I haven't found a reliable way to do this only at application 
    // shutdown
    _renderThread->wait();
}


#define SINGLE_QML_ENGINE 0


#if SINGLE_QML_ENGINE
static QQmlEngine* globalEngine{ nullptr };
static size_t globalEngineRefCount{ 0 };
#endif

QQmlEngine* SharedObject::acquireEngine(OffscreenSurface* surface) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());

    QQmlEngine* result = nullptr;
    if (QThread::currentThread() != qApp->thread()) {
        qCWarning(qmlLogging) << "Cannot acquire QML engine on any thread but the main thread";
    }

#if SINGLE_QML_ENGINE
    if (!globalEngine) {
        Q_ASSERT(0 == globalEngineRefCount);
        globalEngine = new QQmlEngine();
        surface->initializeQmlEngine(result);
        ++globalEngineRefCount;
    }
    result = globalEngine;
#else
    result = new QQmlEngine();
    surface->initializeEngine(result);
#endif

    return result;
}

void SharedObject::releaseEngine(QQmlEngine* engine) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
#if SINGLE_QML_ENGINE
    Q_ASSERT(0 != globalEngineRefCount);
    if (0 == --globalEngineRefCount) {
        globalEngine->deleteLater();
        globalEngine = nullptr;
    }
#else
    engine->deleteLater();
#endif
}

bool SharedObject::event(QEvent* e) {
    switch (static_cast<OffscreenEvent::Type>(e->type())) {
        case OffscreenEvent::Initialize:
            onInitialize();
            return true;

        case OffscreenEvent::Render:
            onRender();
            return true;

        default:
            break;
    }
    return QObject::event(e);
}

// Called by the render event handler, from the render thread
void SharedObject::initializeRenderControl(QOpenGLContext* context) {
    if (context->shareContext() != getSharedContext()) {
        qFatal("QML rendering context has no share context");
    }

    if (!nsightActive()) {
        _renderControl->initialize(context);
    }
}

void SharedObject::releaseTextureAndFence() {
    QMutexLocker lock(&_mutex);
    // If the most recent texture was unused, we can directly recycle it
    if (_latestTextureAndFence.first) {
        offscreenTextures.releaseTexture(_latestTextureAndFence);
        _latestTextureAndFence = TextureAndFence{ 0, 0 };
    }
}

void SharedObject::setRenderTarget(uint32_t fbo, const QSize& size) {
    _quickWindow->setRenderTarget(fbo, size);
}

QSize SharedObject::getSize() const {
    QMutexLocker locker(&_mutex);
    return _size;
}

void SharedObject::setSize(const QSize& size) {
    if (getSize() == size) {
        return;
    }

    {
        QMutexLocker locker(&_mutex);
        _size = size;
    }

    qCDebug(qmlLogging) << "Offscreen UI resizing to " << size.width() << "x" << size.height();
    _quickWindow->setGeometry(QRect(QPoint(), size));
    _quickWindow->contentItem()->setSize(size);

    if (_rootItem) {
        _qmlContext->setContextProperty("surfaceSize", size);
        _rootItem->setSize(size);
    }

    requestRenderSync();
}

bool SharedObject::preRender() {
    QMutexLocker lock(&_mutex);
    if (_paused) {
        if (_syncRequested) {
            wake();
        }
        return false;
    }

    if (_syncRequested) {
        bool syncResult = true;
        if (!nsightActive()) {
            PROFILE_RANGE(render_qml_gl, "sync")
            syncResult = _renderControl->sync();
        }
        wake();
        if (!syncResult) {
            return false;
        }
        _syncRequested = false;
    }

    return true;
}

void SharedObject::shutdownRendering(OffscreenGLCanvas& canvas, const QSize& size) {
    QMutexLocker locker(&_mutex);
    if (size != QSize(0, 0)) {
        offscreenTextures.releaseSize(size);
    }
    _renderControl->invalidate();
    canvas.doneCurrent();
    wake();
}

bool SharedObject::isQuit() {
    QMutexLocker locker(&_mutex);
    return _quit;
}

void SharedObject::requestRender() {
    // Don't queue multiple renders
    if (_renderRequested) {
        return;
    }
    _renderRequested = true;
}

void SharedObject::requestRenderSync() {
    if (_quit) {
        return;
    }

    {
        QMutexLocker lock(&_mutex);
        _syncRequested = true;
    }

    requestRender();
}

bool SharedObject::fetchTexture(TextureAndFence& textureAndFence) {
    QMutexLocker locker(&_mutex);
    if (0 == _latestTextureAndFence.first) {
        return false;
    }
    textureAndFence = { 0, 0 };
    std::swap(textureAndFence, _latestTextureAndFence);
    return true;
}

void SharedObject::setProxyWindow(QWindow* window) {
    _proxyWindow = window;
    _renderControl->setRenderWindow(window);
}

void SharedObject::wait() {
    _cond.wait(&_mutex);
}

void SharedObject::wake() {
    _cond.wakeOne();
}

void SharedObject::onInitialize() {
    // Associate root item with the window.
    _rootItem->setParentItem(_quickWindow->contentItem());
    _renderControl->prepareThread(_renderThread);

    // Set up the render thread
    QCoreApplication::postEvent(_renderObject, new OffscreenEvent(OffscreenEvent::Initialize));

    requestRender();

    // Set up timer to trigger renders
    _renderTimer = new QTimer(this);
    QObject::connect(_renderTimer, &QTimer::timeout, this, &SharedObject::onTimer);

    _renderTimer->setTimerType(Qt::PreciseTimer);
    _renderTimer->setInterval(MIN_TIMER_MS);  // 5ms, Qt::PreciseTimer required
    _renderTimer->start();
}

void SharedObject::onRender() {
    PROFILE_RANGE(render_qml, __FUNCTION__);
    if (_quit) {
        return;
    }

    QMutexLocker lock(&_mutex);
    if (_syncRequested) {
        lock.unlock();
        _renderControl->polishItems();
        lock.relock();
        QCoreApplication::postEvent(_renderObject, new OffscreenEvent(OffscreenEvent::Render));
        // sync and render request, main and render threads must be synchronized
        wait();
    } else {
        QCoreApplication::postEvent(_renderObject, new OffscreenEvent(OffscreenEvent::Render));
    }
    _renderRequested = false;
}

void SharedObject::onTimer() {
    offscreenTextures.report();
    if (!_renderRequested) {
        return;
    }

    {
        QMutexLocker locker(&_mutex);
        // Don't queue more than one frame at a time
        if (0 != _latestTextureAndFence.first) {
            return;
        }
    }

    {
        auto minRenderInterval = USECS_PER_SECOND / _maxFps;
        auto lastInterval = usecTimestampNow() - _lastRenderTime;
        // Don't exceed the framerate limit
        if (lastInterval < minRenderInterval) {
            return;
        }
    }

    QCoreApplication::postEvent(this, new OffscreenEvent(OffscreenEvent::Render));
}

void SharedObject::onAboutToQuit() {
    destroy();
}

void SharedObject::updateTextureAndFence(const TextureAndFence& newTextureAndFence) {
    QMutexLocker locker(&_mutex);
    // If the most recent texture was unused, we can directly recycle it
    if (_latestTextureAndFence.first) {
        offscreenTextures.releaseTexture(_latestTextureAndFence);
        _latestTextureAndFence = { 0, 0 };
    }

    _latestTextureAndFence = newTextureAndFence;
}

void SharedObject::pause() {
    _paused = true;
}

void SharedObject::resume() {
    _paused = false;
    requestRender();
}

bool SharedObject::isPaused() const {
    return _paused;
}
