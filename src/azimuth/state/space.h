/*=============================================================================
| Copyright 2012 Matthew D. Steele <mdsteele@alum.mit.edu>                    |
|                                                                             |
| This file is part of Azimuth.                                               |
|                                                                             |
| Azimuth is free software: you can redistribute it and/or modify it under    |
| the terms of the GNU General Public License as published by the Free        |
| Software Foundation, either version 3 of the License, or (at your option)   |
| any later version.                                                          |
|                                                                             |
| Azimuth is distributed in the hope that it will be useful, but WITHOUT      |
| ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       |
| FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for   |
| more details.                                                               |
|                                                                             |
| You should have received a copy of the GNU General Public License along     |
| with Azimuth.  If not, see <http://www.gnu.org/licenses/>.                  |
=============================================================================*/

#pragma once
#ifndef AZIMUTH_STATE_SPACE_H_
#define AZIMUTH_STATE_SPACE_H_

#include <stdbool.h>

#include "azimuth/state/baddie.h"
#include "azimuth/state/door.h"
#include "azimuth/state/node.h"
#include "azimuth/state/particle.h"
#include "azimuth/state/pickup.h"
#include "azimuth/state/planet.h"
#include "azimuth/state/player.h"
#include "azimuth/state/projectile.h"
#include "azimuth/state/room.h"
#include "azimuth/state/ship.h"
#include "azimuth/state/uid.h"
#include "azimuth/state/wall.h"
#include "azimuth/util/vector.h"

/*===========================================================================*/

typedef struct {
  double active_for; // negative if timer is inactive
  double time_remaining; // seconds
} az_timer_t;

typedef struct {
  const az_planet_t *planet;
  unsigned long clock;
  az_vector_t camera;
  az_ship_t ship;
  az_timer_t timer;

  // Mode information:
  enum {
    AZ_MODE_NORMAL, // flying around; the normal mode of gameplay
    AZ_MODE_DOORWAY, // waiting while we pass through a door
    AZ_MODE_GAME_OVER // showing the game over animation
  } mode;
  union {
    struct {
      enum { AZ_DWS_FADE_OUT, AZ_DWS_SHIFT, AZ_DWS_FADE_IN } step;
      double progress; // 0.0 to 1.0
      const az_door_t *door;
    } doorway;
    struct {
      enum { AZ_GOS_FADE_OUT, AZ_GOS_ASPLODE } step;
      double progress; // 0.0 to 1.0
    } game_over;
  } mode_data;

  // Space objects (these all get cleared out when we exit a room):
  az_baddie_t baddies[100];
  az_door_t doors[20];
  az_node_t nodes[50];
  az_particle_t particles[500];
  az_pickup_t pickups[100];
  az_projectile_t projectiles[250];
  az_wall_t walls[250];
} az_space_state_t;

// Remove all objects (baddies, doors, etc.), but leave other fields unchanged.
void az_clear_space(az_space_state_t *state);

// Add all room objects to the space state, on top of whatever objects are
// already there.  You may want to call az_clear_space first to ensure that
// there is room for the new objects.  Note that this function does not make
// any changes to the ship or any other fields.
void az_enter_room(az_space_state_t *state, const az_room_t *room);

bool az_insert_baddie(az_space_state_t *state, az_baddie_t **baddie_out);

bool az_insert_door(az_space_state_t *state, az_door_t **door_out);

bool az_lookup_node(az_space_state_t *state, az_uid_t uid,
                    az_node_t **node_out);
bool az_insert_node(az_space_state_t *state, az_node_t **node_out);

bool az_insert_particle(az_space_state_t *state, az_particle_t **particle_out);

bool az_insert_projectile(az_space_state_t *state, az_projectile_t **proj_out);

bool az_insert_wall(az_space_state_t *state, az_wall_t **wall_out);

// Add a pickup of the given kind at the given position.  If the pickups array
// is already full, this silently does nothing.
void az_try_add_pickup(az_space_state_t *state, az_pickup_kind_t kind,
                       az_vector_t position);

/*===========================================================================*/

#endif // AZIMUTH_STATE_SPACE_H_
