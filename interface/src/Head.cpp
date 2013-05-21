//
//  Head.cpp
//  interface
//
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.

#include "Head.h"
#include "Util.h"
#include <vector>
#include <SharedUtil.h>
#include <lodepng.h>

using namespace std;

const float HEAD_MOTION_DECAY = 0.1;
const float MINIMUM_EYE_ROTATION = 0.7f; // based on a dot product: 1.0 is straight ahead, 0.0 is 90 degrees off

const float EYEBALL_RADIUS  = 0.02; 
const float IRIS_RADIUS     = 0.007;
const float IRIS_PROTRUSION = 0.018f;

float _browColor [] = {210.0/255.0, 105.0/255.0, 30.0/255.0};
float _mouthColor[] = {1, 0, 0};

float _BrowRollAngle [5] = {  0.0f,  15.0f, 30.0f, -30.0f, -15.0f};
float _BrowPitchAngle[3] = {-70.0f, -60.0f, -50.0f};
float _eyeColor      [3] = {  0.9f,  0.9f,   0.8f};

float _MouthWidthChoices[3] = {0.5, 0.77, 0.3};

float _browWidth = 0.8;
float _browThickness = 0.16;

const char IRIS_TEXTURE_FILENAME[] = "resources/images/iris.png";
unsigned int IRIS_TEXTURE_WIDTH = 768;
unsigned int IRIS_TEXTURE_HEIGHT = 498;
vector<unsigned char> irisTexture;

Head::Head() :
    yawRate(0.0f),
    noise(0.0f),
    _audioLoudness(0.0f),
    _skinColor(0.0f, 0.0f, 0.0f),
    _position(0.0f, 0.0f, 0.0f),
    _rotation(0.0f, 0.0f, 0.0f),
    _lookatPosition(0.0f, 0.0f, 0.0f),
    //_yaw(0.0f),
    //_pitch(0.0f),
    //_roll(0.0f),
    _eyeballPitch(),
    _eyeballYaw(),
    _interBrowDistance(0.75f),
    _mouthPitch(0),
    _mouthYaw(0),
    _mouthWidth(1.0f),
    _mouthHeight(0.2f),
    //_pitchTarget(0.0f),
    //_yawTarget(0.0f),
    _noiseEnvelope(1.0f),
    _scale(1.0f),
    _eyeContact(1),
    _browAudioLift(0.0f),
    _gravity(0.0f, -1.0f, 0.0f),
    _lastLoudness(0.0f),
    _averageLoudness(0.0f),
    _audioAttack(0.0f),
    _returnSpringScale(1.0f),
    _bodyRotation(0.0f, 0.0f, 0.0f),
    _headRotation(0.0f, 0.0f, 0.0f),
    _eyeContactTarget(LEFT_EYE)
{
    _eyebrowPitch[0]  = -30;
    _eyebrowPitch[1]  = -30;
    _eyebrowRoll [0]  =  20;
    _eyebrowRoll [1]  = -20;
}

/*
void Head::setPositionRotationAndScale(glm::vec3 p, glm::vec3 r, float s) {
    _position = p;
    _scale    = s;
    _headRotation = r;
}
*/

/*
void Head::setNewTarget(float pitch, float yaw) {
    _pitchTarget = pitch;
    _yawTarget   = yaw;
}
*/

void Head::reset() {
    _yaw = _pitch = _roll = 0.0f;
    _leanForward = _leanSideways = 0.0f;
}

void Head::simulate(float deltaTime, bool isMine) {

    //generate orientation directions based on Euler angles...
    _orientation.setToPitchYawRoll
    (
                            _headRotation.x, 
        _bodyRotation.y +   _headRotation.y, 
                            _headRotation.z
    );

    //calculate the eye positions (algorithm still being designed)
    updateEyePositions();

    //  Decay head back to center if turned on
    if (isMine && _returnHeadToCenter) {
        //  Decay back toward center
        _headRotation.x *= (1.0f - HEAD_MOTION_DECAY * _returnSpringScale * 2 * deltaTime);
        _headRotation.y *= (1.0f - HEAD_MOTION_DECAY * _returnSpringScale * 2 * deltaTime);
        _headRotation.z *= (1.0f - HEAD_MOTION_DECAY * _returnSpringScale * 2 * deltaTime);
    }
    
    //  For invensense gyro, decay only slightly when roughly centered
    if (isMine) {
        const float RETURN_RANGE = 15.0;
        const float RETURN_STRENGTH = 2.0;
        if (fabs(_headRotation.x) < RETURN_RANGE) { _headRotation.x *= (1.0f - RETURN_STRENGTH * deltaTime); }
        if (fabs(_headRotation.y) < RETURN_RANGE) { _headRotation.y *= (1.0f - RETURN_STRENGTH * deltaTime); }
        if (fabs(_headRotation.z) < RETURN_RANGE) { _headRotation.z *= (1.0f - RETURN_STRENGTH * deltaTime); }
    }

    /*
    if (noise) {
        //  Move toward new target
        _pitch += (_pitchTarget - _pitch) * 10 * deltaTime; // (1.f - DECAY*deltaTime)*Pitch + ;
        _yaw   += (_yawTarget   - _yaw  ) * 10 * deltaTime; // (1.f - DECAY*deltaTime);
        _roll *= 1.f - (HEAD_MOTION_DECAY * deltaTime);
    }
    */
    
    _leanForward  *= (1.f - HEAD_MOTION_DECAY * 30 * deltaTime);
    _leanSideways *= (1.f - HEAD_MOTION_DECAY * 30 * deltaTime);
        
    //  Update where the avatar's eyes are
    //
    //  First, decide if we are making eye contact or not
    if (randFloat() < 0.005) {
        _eyeContact = !_eyeContact;
        _eyeContact = 1;
        if (!_eyeContact) {
            //  If we just stopped making eye contact,move the eyes markedly away
            _eyeballPitch[0] = _eyeballPitch[1] = _eyeballPitch[0] + 5.0 + (randFloat() - 0.5) * 10;
            _eyeballYaw  [0] = _eyeballYaw  [1] = _eyeballYaw  [0] + 5.0 + (randFloat() - 0.5) * 5;
        } else {
            //  If now making eye contact, turn head to look right at viewer
            //setNewTarget(0,0);
        }
    }
    
    const float DEGREES_BETWEEN_VIEWER_EYES = 3;
    const float DEGREES_TO_VIEWER_MOUTH = 7;
    
    if (_eyeContact) {
        //  Should we pick a new eye contact target?
        if (randFloat() < 0.01) {
            //  Choose where to look next
            if (randFloat() < 0.1) {
                _eyeContactTarget = MOUTH;
            } else {
                if (randFloat() < 0.5) {
                    _eyeContactTarget = LEFT_EYE; 
                } else {
                    _eyeContactTarget = RIGHT_EYE;
                }
            }
        }
        
        //  Set eyeball pitch and yaw to make contact
        float eye_target_yaw_adjust   = 0.0f;
        float eye_target_pitch_adjust = 0.0f;

        if (_eyeContactTarget == LEFT_EYE ) { eye_target_yaw_adjust   =  DEGREES_BETWEEN_VIEWER_EYES; }
        if (_eyeContactTarget == RIGHT_EYE) { eye_target_yaw_adjust   = -DEGREES_BETWEEN_VIEWER_EYES; }
        if (_eyeContactTarget == MOUTH    ) { eye_target_pitch_adjust =  DEGREES_TO_VIEWER_MOUTH;     }
        
        _eyeballPitch[0] = _eyeballPitch[1] = -_headRotation.x + eye_target_pitch_adjust;
        _eyeballYaw  [0] = _eyeballYaw  [1] =  _headRotation.y + eye_target_yaw_adjust;
    }
    
    if (noise)
    {
        _headRotation.x += (randFloat() - 0.5) * 0.2 * _noiseEnvelope;
        _headRotation.y += (randFloat() - 0.5) * 0.3 *_noiseEnvelope;
        //PupilSize += (randFloat() - 0.5) * 0.001*NoiseEnvelope;
        
        if (randFloat() < 0.005) _mouthWidth = _MouthWidthChoices[rand()%3];
        
        if (!_eyeContact) {
            if (randFloat() < 0.01)  _eyeballPitch[0] = _eyeballPitch[1] = (randFloat() - 0.5) * 20;
            if (randFloat() < 0.01)  _eyeballYaw[0] = _eyeballYaw[1] = (randFloat()- 0.5) * 10;
        }
        
        /*
        if ((randFloat() < 0.005) && (fabs(_pitchTarget - _pitch) < 1.0) && (fabs(_yawTarget - _yaw) < 1.0)) {
            setNewTarget((randFloat()-0.5) * 20.0, (randFloat()-0.5) * 45.0);
        }
        */
        
        if (0) {
            
            //  Pick new target
            //_pitchTarget = (randFloat() - 0.5) * 45;
            //_yawTarget = (randFloat() - 0.5) * 22;
        }
        if (randFloat() < 0.01)
        {
            _eyebrowPitch[0] = _eyebrowPitch[1] = _BrowPitchAngle[rand()%3];
            _eyebrowRoll [0] = _eyebrowRoll[1] = _BrowRollAngle[rand()%5];
            _eyebrowRoll [1] *=-1;
        }
    }
    
    //  Update audio trailing average for rendering facial animations
    const float AUDIO_AVERAGING_SECS = 0.05;
    _averageLoudness = (1.f - deltaTime / AUDIO_AVERAGING_SECS) * _averageLoudness +
                             (deltaTime / AUDIO_AVERAGING_SECS) * _audioLoudness;
                                                        
}

void Head::updateEyePositions() {
    float rightShift = _scale * 0.27f;
    float upShift    = _scale * 0.38f;
    float frontShift = _scale * 0.8f;
    
    _leftEyePosition  = _position 
                      - _orientation.getRight() * rightShift 
                      + _orientation.getUp   () * upShift 
                      + _orientation.getFront() * frontShift;
    _rightEyePosition = _position
                      + _orientation.getRight() * rightShift 
                      + _orientation.getUp   () * upShift 
                      + _orientation.getFront() * frontShift;
}


void Head::setLooking(bool looking) {

    _lookingAtSomething = looking;

    glm::vec3 averageEyePosition = _leftEyePosition + (_rightEyePosition - _leftEyePosition ) * ONE_HALF;
    glm::vec3 targetLookatAxis = glm::normalize(_lookAtPosition - averageEyePosition);
    
    float dot = glm::dot(targetLookatAxis, _orientation.getFront());
    if (dot < MINIMUM_EYE_ROTATION) {
        _lookingAtSomething = false;
    }
}


void Head::render(bool lookingInMirror) {

    //int side = 0;
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_RESCALE_NORMAL);
    
    renderEars();
    
        
    glPushMatrix();
    
        glTranslatef(_position.x, _position.y, _position.z); //translate to head position
        glScalef(_scale, _scale, _scale); //scale to head size
    
    
    /*
    if (lookingInMirror) {
        glRotatef(_bodyRotation.y - _yaw, 0, 1, 0);
        glRotatef(_pitch,  1, 0, 0);   
        glRotatef(-_roll,  0, 0, 1);
    } else {
        glRotatef(_bodyRotation.y + _yaw, 0, 1, 0);
        glRotatef(_pitch, 1, 0, 0);
        glRotatef(_roll,  0, 0, 1);
    }*/

    //glRotatef(                  _headRotation.x, IDENTITY_RIGHT.x, IDENTITY_RIGHT.y, IDENTITY_RIGHT.z);
    //glRotatef(_bodyRotation.y + _headRotation.y, IDENTITY_UP.x,    IDENTITY_UP.y,    IDENTITY_UP.z   );
    //glRotatef(                  _headRotation.z, IDENTITY_FRONT.x, IDENTITY_FRONT.y, IDENTITY_FRONT.z);
    
    //draw head sphere
    glColor3f(_skinColor.x, _skinColor.y, _skinColor.z);
    glutSolidSphere(1, 30, 30);
    
    /*
    // render ears
    glPushMatrix();
    glTranslatef(1.0, 0, 0);
    for(int side = 0; side < 2; side++) {
        glPushMatrix();
        glScalef(0.3, 0.65, .65);
        glutSolidSphere(0.5, 30, 30);
        glPopMatrix();
        glTranslatef(-2.0, 0, 0);
    }
    glPopMatrix();
    */
    
    
    
    /*
    //  Update audio attack data for facial animation (eyebrows and mouth)
    _audioAttack = 0.9 * _audioAttack + 0.1 * fabs(_audioLoudness - _lastLoudness);
    _lastLoudness = _audioLoudness;
    
    const float BROW_LIFT_THRESHOLD = 100;
    if (_audioAttack > BROW_LIFT_THRESHOLD)
        _browAudioLift += sqrt(_audioAttack) / 1000.0;
    
    _browAudioLift *= .90;
    
    //  Render Eyebrows
    glPushMatrix();
    glTranslatef(-_interBrowDistance / 2.0,0.4,0.45);
    for(int side = 0; side < 2; side++) {
        glColor3fv(_browColor);
        glPushMatrix();
        glTranslatef(0, 0.35 + _browAudioLift, 0);
        glRotatef(_eyebrowPitch[side]/2.0, 1, 0, 0);
        glRotatef(_eyebrowRoll[side]/2.0, 0, 0, 1);
        glScalef(_browWidth, _browThickness, 1);
        glutSolidCube(0.5);
        glPopMatrix();
        glTranslatef(_interBrowDistance, 0, 0);
    }
    glPopMatrix();
    */
    
    
    
    
    
    
    
    /*
    // Mouth
//    const float MIN_LOUDNESS_SCALE_WIDTH = 0.7f;
//    const float WIDTH_SENSITIVITY = 60.f;
//    const float HEIGHT_SENSITIVITY = 30.f;
//    const float MIN_LOUDNESS_SCALE_HEIGHT = 1.0f;
    glPushMatrix();
        glTranslatef(0,-0.35,0.75);
        glColor3f(0,0,0);

        glRotatef(_mouthPitch, 1, 0, 0);
        glRotatef(_mouthYaw, 0, 0, 1);

        if (_averageLoudness > 1.f) {
            glScalef(_mouthWidth  * (.7f + sqrt(_averageLoudness) /60.f),
                     _mouthHeight * (1.f + sqrt(_averageLoudness) /30.f), 1);
        } else {
            glScalef(_mouthWidth, _mouthHeight, 1);
        } 

        glutSolidCube(0.5);
    glPopMatrix();
    */
    
    glPopMatrix();

    renderEyeBalls();    
        
    /*
    if (_lookingAtSomething) {
        // Render lines originating from the eyes and converging on the lookatPosition    
        debugRenderLookatVectors(_leftEyePosition, _rightEyePosition, _lookatPosition);
    }
    */
}


void Head::renderEars() {

    glColor3f(_skinColor.x, _skinColor.y, _skinColor.z);
    glPushMatrix();
        glTranslatef(_position.x -_orientation.getRight().x * _scale, _position.x -_orientation.getRight().y * _scale, _position.x -_orientation.getRight().z * _scale);
        //glScalef(0.3, 0.65, .65);
        glutSolidSphere(0.01, 30, 30);
    glPopMatrix();

    glPushMatrix();
        glTranslatef(_position.x  + _orientation.getRight().x, _position.x  + _orientation.getRight().y, _position.x  + _orientation.getRight().z);
        //glScalef(0.3, 0.65, .65);
        glutSolidSphere(0.01, 30, 30);
    glPopMatrix();
}


void Head::renderEyeBalls() {                                 
    
    if (::irisTexture.size() == 0) {
        switchToResourcesParentIfRequired();
        unsigned error = lodepng::decode(::irisTexture, IRIS_TEXTURE_WIDTH, IRIS_TEXTURE_HEIGHT, IRIS_TEXTURE_FILENAME);
        if (error != 0) {
            printLog("error %u: %s\n", error, lodepng_error_text(error));
        }
    }
    
    // setup the texutre to be used on each iris
    GLUquadric* irisQuadric = gluNewQuadric();
    gluQuadricTexture(irisQuadric, GL_TRUE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gluQuadricOrientation(irisQuadric, GLU_OUTSIDE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IRIS_TEXTURE_WIDTH, IRIS_TEXTURE_HEIGHT,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, &::irisTexture[0]);

    // render white ball of left eyeball
    glPushMatrix();
        glColor3fv(_eyeColor);
        glTranslatef(_leftEyePosition.x, _leftEyePosition.y, _leftEyePosition.z);        
        gluSphere(irisQuadric, EYEBALL_RADIUS, 30, 30);
    glPopMatrix();
    
    // render left iris
    glPushMatrix(); {
        glTranslatef(_leftEyePosition.x, _leftEyePosition.y, _leftEyePosition.z); //translate to eyeball position
        
        glPushMatrix();
        
            if (_lookingAtSomething) {

                //rotate the eyeball to aim towards the lookat position
                glm::vec3 targetLookatAxis = glm::normalize(_lookAtPosition - _leftEyePosition); // the lookat direction
                glm::vec3 rotationAxis = glm::cross(targetLookatAxis, IDENTITY_UP);
                float angle = 180.0f - angleBetween(targetLookatAxis, IDENTITY_UP);            
                glRotatef(angle, rotationAxis.x, rotationAxis.y, rotationAxis.z);
                glRotatef(180.0, 0.0f, 1.0f, 0.0f); //adjust roll to correct after previous rotations
            } else {

                //rotate the eyeball to aim straight ahead
                glm::vec3 rotationAxisToHeadFront = glm::cross(_orientation.getFront(), IDENTITY_UP);            
                float angleToHeadFront = 180.0f - angleBetween(_orientation.getFront(), IDENTITY_UP);            
                glRotatef(angleToHeadFront, rotationAxisToHeadFront.x, rotationAxisToHeadFront.y, rotationAxisToHeadFront.z);

                //set the amount of roll (for correction after previous rotations)
                float rollRotation = angleBetween(_orientation.getFront(), IDENTITY_FRONT);            
                float dot = glm::dot(_orientation.getFront(), -IDENTITY_RIGHT);
                if ( dot < 0.0f ) { rollRotation = -rollRotation; }
                glRotatef(rollRotation, 0.0f, 1.0f, 0.0f); //roll the iris or correct roll about the lookat vector
            }
             
            glTranslatef( 0.0f, -IRIS_PROTRUSION, 0.0f);//push the iris out a bit (otherwise - inside of eyeball!) 
            glScalef( 1.0f, 0.5f, 1.0f); // flatten the iris 
            glEnable(GL_TEXTURE_2D);
            gluSphere(irisQuadric, IRIS_RADIUS, 15, 15);
            glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
    glPopMatrix();

    //render white ball of right eyeball
    glPushMatrix();
        glColor3fv(_eyeColor);
        glTranslatef(_rightEyePosition.x, _rightEyePosition.y, _rightEyePosition.z);        
        gluSphere(irisQuadric, EYEBALL_RADIUS, 30, 30);
    glPopMatrix();

    // render right iris
    glPushMatrix(); {
        glTranslatef(_rightEyePosition.x, _rightEyePosition.y, _rightEyePosition.z);  //translate to eyeball position       

        glPushMatrix();
        
            if (_lookingAtSomething) {
            
                //rotate the eyeball to aim towards the lookat position
                glm::vec3 targetLookatAxis = glm::normalize(_lookAtPosition - _rightEyePosition);
                glm::vec3 rotationAxis = glm::cross(targetLookatAxis, IDENTITY_UP);
                float angle = 180.0f - angleBetween(targetLookatAxis, IDENTITY_UP);            
                glRotatef(angle, rotationAxis.x, rotationAxis.y, rotationAxis.z);
                glRotatef(180.0f, 0.0f, 1.0f, 0.0f); //adjust roll to correct after previous rotations
            } else {

                //rotate the eyeball to aim straight ahead
                glm::vec3 rotationAxisToHeadFront = glm::cross(_orientation.getFront(), IDENTITY_UP);            
                float angleToHeadFront = 180.0f - angleBetween(_orientation.getFront(), IDENTITY_UP);            
                glRotatef(angleToHeadFront, rotationAxisToHeadFront.x, rotationAxisToHeadFront.y, rotationAxisToHeadFront.z);

                //set the amount of roll (for correction after previous rotations)
                float rollRotation = angleBetween(_orientation.getFront(), IDENTITY_FRONT); 
                float dot = glm::dot(_orientation.getFront(), -IDENTITY_RIGHT);
                if ( dot < 0.0f ) { rollRotation = -rollRotation; }
                glRotatef(rollRotation, 0.0f, 1.0f, 0.0f); //roll the iris or correct roll about the lookat vector
            }
            
            glTranslatef( 0.0f, -IRIS_PROTRUSION, 0.0f);//push the iris out a bit (otherwise - inside of eyeball!) 
            glScalef( 1.0f, 0.5f, 1.0f); // flatten the iris 
            glEnable(GL_TEXTURE_2D);
            gluSphere(irisQuadric, IRIS_RADIUS, 15, 15);
            glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
    
    // delete the iris quadric now that we're done with it
    gluDeleteQuadric(irisQuadric);
    glPopMatrix();
}

void Head::debugRenderLookatVectors(glm::vec3 leftEyePosition, glm::vec3 rightEyePosition, glm::vec3 lookatPosition) {

    glColor3f(0.0f, 0.0f, 0.0f);
    glLineWidth(3.0);
    glBegin(GL_LINE_STRIP);
    glVertex3f(leftEyePosition.x, leftEyePosition.y, leftEyePosition.z);
    glVertex3f(lookatPosition.x, lookatPosition.y, lookatPosition.z);
    glEnd();
    glBegin(GL_LINE_STRIP);
    glVertex3f(rightEyePosition.x, rightEyePosition.y, rightEyePosition.z);
    glVertex3f(lookatPosition.x, lookatPosition.y, lookatPosition.z);
    glEnd();
}



