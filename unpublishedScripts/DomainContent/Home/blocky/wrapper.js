//  createPingPongGun.js
//
//  Script Type: Entity Spawner
//  Created by James B. Pollack on  9/30/2015
//  Copyright 2015 High Fidelity, Inc.
//
//  This script creates a gun that shoots ping pong balls when you pull the trigger on a hand controller.
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//


var RESETTER_POSITION = {
  x: 1098.5159,
  y: 460.0490,
  z: -66.3012
};

BlockyGame = function(spawnPosition, spawnRotation) {

  var scriptURL = "atp:/blocky/blocky.js";

  var blockyProps = {
    type: 'Box',
    name: 'home_box_blocky_resetter',
    color: {
      red: 0,
      green: 0,
      blue: 255
    },
    dimensions: {
      x: 0.25,
      y: 0.25,
      z: 0.25
    },
    rotation:Quat.fromPitchYawRollDegrees(-0.0029,32.9983,-0.0021),
    script: scriptURL,
    userData: JSON.stringify({
      "grabbableKey": {
        "wantsTrigger": true
      },
      'hifiHomeKey': {
        'reset': true
      }
    }),
    dynamic: false,
    position: spawnPosition
  };

  var blocky = Entities.addEntity(blockyProps);

  function cleanup() {
    print('BLOCKY CLEANUP!')
    Entities.deleteEntity(blocky);
  }

  this.cleanup = cleanup;

  print('HOME CREATED BLOCKY GAME BLOCK 1')

}