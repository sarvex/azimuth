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

#include "azimuth/tick/baddie_core.h"

#include <assert.h>
#include <math.h>

#include "azimuth/state/baddie.h"
#include "azimuth/state/sound.h"
#include "azimuth/state/space.h"
#include "azimuth/tick/baddie_util.h"
#include "azimuth/tick/object.h"
#include "azimuth/util/random.h"
#include "azimuth/util/vector.h"

/*===========================================================================*/

#define INITIAL_STATE 0
#define START_DORMANCY_STATE 1
#define DORMANT_STATE 2
#define CHARGE_BEAM_FORWARD_STATE 3
#define CHARGE_BEAM_BACKWARD_STATE 4
#define FIRE_BEAM_FORWARD_STATE 5
#define FIRE_BEAM_BACKWARD_STATE 6
#define PILLBOX_STATE 7
#define PRISMATIC_STATE 8

/*===========================================================================*/

static void move_component_towards(az_baddie_t *baddie, double time, int idx,
                                   az_vector_t goal_position,
                                   double goal_angle) {
  az_component_t *component = &baddie->components[idx];
  az_vpluseq(&component->position,
             az_vcaplen(az_vsub(goal_position, component->position),
                        30.0 * time));
  component->angle = az_angle_towards(component->angle, AZ_DEG2RAD(45) * time,
                                      goal_angle);
}

static void adjust_to_beam_configuration(az_baddie_t *baddie, double time) {
  for (int i = 0; i < 8; ++i) {
    const az_vector_t goal = {0, (i < 4 ? 20 : -20)};
    move_component_towards(baddie, time, i, goal, i * AZ_DEG2RAD(45));
  }
}

static void adjust_to_pillbox_configuration(az_baddie_t *baddie, double time) {
  for (int i = 0; i < 8; ++i) {
    const az_vector_t goal =
      az_vpolar(40, AZ_DEG2RAD(22.5) + i * AZ_DEG2RAD(45));
    move_component_towards(baddie, time, i, goal, i * AZ_DEG2RAD(45));
  }
}

static void adjust_to_sawblade_configuration(az_baddie_t *baddie,
                                             double time) {
  for (int i = 0; i < 8; ++i) {
    const az_vector_t goal_position = az_vpolar(110, i * AZ_DEG2RAD(45));
    const double goal_angle = az_mod2pi(AZ_DEG2RAD(85) + i * AZ_DEG2RAD(45));
    move_component_towards(baddie, time, i, goal_position, goal_angle);
  }
}

static void start_charging_beam(az_space_state_t *state, az_baddie_t *baddie) {
  baddie->state = (az_vdot(az_vsub(state->ship.position, baddie->position),
                           az_vpolar(1.0, baddie->angle)) >= 0.0 ?
                   CHARGE_BEAM_FORWARD_STATE : CHARGE_BEAM_BACKWARD_STATE);
  baddie->cooldown = 1.0;
  az_play_sound(&state->soundboard, AZ_SND_CORE_BEAM_CHARGE);
}

// Turn the angle_offset side of the baddie towards the ship.
static void turn_offset_towards_ship(
    az_space_state_t *state, az_baddie_t *baddie, double time,
    double angle_offset) {
  baddie->angle = az_angle_towards(
      baddie->angle, AZ_DEG2RAD(30) * time,
      az_mod2pi(az_vtheta(az_vsub(state->ship.position, baddie->position)) +
                angle_offset));
}

static bool double_towards(double *value, double delta, double goal) {
  assert(delta >= 0.0);
  if (*value < goal - delta) *value += delta;
  else if (*value > goal + delta) *value -= delta;
  else {
    *value = goal;
    return true;
  }
  return false;
}

static bool adjust_gravity(az_space_state_t *state, double time,
                           double goal, bool rotational) {
  const double delta = 165.0 * time;
  az_gravfield_t *pull_grav = NULL;
  az_gravfield_t *spin_grav = NULL;
  AZ_ARRAY_LOOP(gravfield, state->gravfields) {
    if (gravfield->kind == AZ_GRAV_SECTOR_PULL) pull_grav = gravfield;
    else if (gravfield->kind == AZ_GRAV_SECTOR_SPIN) spin_grav = gravfield;
  }
  az_gravfield_t *reduce_grav = (rotational ? pull_grav : spin_grav);
  az_gravfield_t *adjust_grav = (rotational ? spin_grav : pull_grav);
  if (reduce_grav != NULL &&
      !double_towards(&reduce_grav->strength, delta, 0.0)) return false;
  if (adjust_grav != NULL &&
      !double_towards(&adjust_grav->strength, delta, goal)) return false;
  return true;
}

static void spin_gravity_back_and_forth(
    az_space_state_t *state, az_baddie_t *baddie, double time) {
  const double goal = 600.0 * baddie->param;
  if (adjust_gravity(state, time, goal, true)) baddie->param = -baddie->param;
}

static void pull_gravity_in_and_out(
    az_space_state_t *state, az_baddie_t *baddie, double time) {
  const double goal = 500.0 * baddie->param;
  if (adjust_gravity(state, time, goal, false)) baddie->param = -baddie->param;
}

/*===========================================================================*/

static void charge_rainbow_beam(
    az_space_state_t *state, az_baddie_t *baddie, double time,
    double angle_offset, int next_state) {
  adjust_to_beam_configuration(baddie, time);
  spin_gravity_back_and_forth(state, baddie, time);
  const double particle_lifetime = 0.5;
  const double particle_distance = 50.0;
  if (baddie->cooldown >= particle_lifetime) {
    const az_vector_t center =
      az_vadd(baddie->position, az_vpolar(100, baddie->angle + angle_offset));
    const double angle = 145.0 * baddie->cooldown;
    az_particle_t *particle;
    if (az_insert_particle(state, &particle)) {
      particle->kind = AZ_PAR_EMBER;
      particle->color = az_hsva_color(2.0 * angle, 0.5, 1.0, 0.75);
      particle->position =
        az_vadd(center, az_vpolar(particle_distance, angle));
      particle->velocity =
        az_vpolar(-particle_distance / particle_lifetime, angle);
      particle->angle = angle;
      particle->lifetime = particle_lifetime;
      particle->param1 = 7.0;
    }
  }
  if (baddie->cooldown <= 0.0) {
    baddie->state = next_state;
    baddie->cooldown = 5.0;
  }
}

static void fire_rainbow_beam(az_space_state_t *state, az_baddie_t *baddie,
                              double time, double angle_offset) {
  adjust_to_beam_configuration(baddie, time);
  spin_gravity_back_and_forth(state, baddie, time);
  turn_offset_towards_ship(state, baddie, time, angle_offset);
  if (baddie->param == 0.0 && baddie->cooldown < 4.5) {
    baddie->param = -copysign(1.0, az_mod2pi(
        az_vtheta(az_vsub(state->ship.position, baddie->position)) -
        (baddie->angle + angle_offset)));
  }

  // Fire a beam, piercing through the ship and other baddies.
  const double beam_angle = baddie->angle + angle_offset;
  const az_vector_t beam_start =
    az_vadd(baddie->position, az_vpolar(100, beam_angle));
  az_impact_t impact;
  az_ray_impact(state, beam_start, az_vpolar(1000, beam_angle),
                (AZ_IMPF_BADDIE | AZ_IMPF_SHIP), baddie->uid, &impact);
  const az_vector_t beam_delta = az_vsub(impact.position, beam_start);
  const double beam_damage = 100.0 * time;
  // Damage the ship and any baddies within the beam.
  if (az_ship_is_alive(&state->ship) &&
      az_ray_hits_ship(&state->ship, beam_start, beam_delta,
                       NULL, NULL)) {
    az_damage_ship(state, beam_damage, false);
    az_loop_sound(&state->soundboard, AZ_SND_BEAM_PHASE);
  }
  AZ_ARRAY_LOOP(other, state->baddies) {
    if (other->kind == AZ_BAD_NOTHING || other == baddie) continue;
    const az_component_data_t *component;
    if (az_ray_hits_baddie(other, beam_start, beam_delta,
                           NULL, NULL, &component)) {
      az_try_damage_baddie(state, other, component,
                           (AZ_DMGF_HYPER_ROCKET | AZ_DMGF_BEAM), beam_damage);
    }
  }
  // Add particles for the beam.
  const az_color_t beam_color = {
    (az_clock_mod(6, 1, state->clock) < 3 ? 255 : 64),
    (az_clock_mod(6, 1, state->clock + 2) < 3 ? 255 : 64),
    (az_clock_mod(6, 1, state->clock + 4) < 3 ? 255 : 64), 192};
  az_add_beam(state, beam_color, beam_start, impact.position, 0.0,
              6 + az_clock_zigzag(6, 1, state->clock));
  for (int i = 0; i < 5; ++i) {
    az_add_speck(state, beam_color, 1.0, impact.position,
                 az_vpolar(az_random(20.0, 70.0),
                           az_vtheta(impact.normal) +
                           az_random(-AZ_HALF_PI, AZ_HALF_PI)));
  }
  az_particle_t *particle;
  if (az_clock_mod(2, 1, state->clock) == 0 &&
      az_insert_particle(state, &particle)) {
    particle->kind = AZ_PAR_EXPLOSION;
    particle->color = beam_color;
    particle->position = impact.position;
    particle->velocity = AZ_VZERO;
    particle->angle = beam_angle;
    particle->lifetime = 0.5;
    particle->param1 = 8.0;
  }
  az_loop_sound(&state->soundboard, AZ_SND_CORE_BEAM_FIRE);

  if (baddie->cooldown <= 0.0) {
    if (baddie->health <= 0.9 * baddie->data->max_health) {
      baddie->state = PILLBOX_STATE;
      baddie->cooldown = 1.0;
    } else start_charging_beam(state, baddie);
  }
}

static void fire_pillbox_rockets(az_space_state_t *state, az_baddie_t *baddie,
                                 double time) {
  adjust_to_pillbox_configuration(baddie, time);
  pull_gravity_in_and_out(state, baddie, time);
  baddie->angle = az_mod2pi(baddie->angle + AZ_DEG2RAD(30) * time);
  if (baddie->cooldown <= 0.0) {
    const double angle = AZ_DEG2RAD(45) *
      round((az_vtheta(az_vsub(state->ship.position, baddie->position)) -
             baddie->angle) / AZ_DEG2RAD(45));
    az_fire_baddie_projectile(state, baddie, AZ_PROJ_ROCKET,
                              100.0, angle, 0.0);
    az_play_sound(&state->soundboard, AZ_SND_FIRE_ROCKET);
    if (baddie->health <= 0.8 * baddie->data->max_health) {
      baddie->state = PRISMATIC_STATE;
      baddie->cooldown = 2.0;
    } else baddie->cooldown = 0.5;
  }
}

static void fire_prismatic_walls(az_space_state_t *state, az_baddie_t *baddie,
                                 double time) {
  adjust_to_sawblade_configuration(baddie, time);
  adjust_gravity(state, time, 400.0, false);
  baddie->angle = az_angle_towards(baddie->angle, AZ_DEG2RAD(90) * time,
                                   AZ_DEG2RAD(22.5));
  if (baddie->cooldown <= 0.0) {
    double start_angle = AZ_DEG2RAD(22.5) + AZ_DEG2RAD(45) *
      round((az_vtheta(az_vsub(state->ship.position, baddie->position)) -
             (baddie->angle + AZ_DEG2RAD(22.5))) / AZ_DEG2RAD(45));
    if (az_randint(0, 4) == 0) start_angle += AZ_DEG2RAD(45);
    for (int i = 0; i < 360; i += 90) {
      az_fire_baddie_projectile(state, baddie, AZ_PROJ_PRISMATIC_WALL,
                                112.0, start_angle + AZ_DEG2RAD(i), 0.0);
    }
    az_play_sound(&state->soundboard, AZ_SND_FIRE_GUN_PIERCE);
    baddie->cooldown = 1.0;
  }
}

void az_tick_bad_zenith_core(az_space_state_t *state, az_baddie_t *baddie,
                             double time) {
  assert(baddie->kind == AZ_BAD_ZENITH_CORE);
  // If the ship accidentally gets caught inside the baddie, knock it away.
  if (az_ship_is_alive(&state->ship) &&
      az_circle_touches_baddie(baddie, AZ_SHIP_DEFLECTOR_RADIUS,
                               state->ship.position, NULL)) {
    const az_vector_t unit =
      az_vunit(az_vsub(state->ship.position, baddie->position));
    if (az_vdot(state->ship.velocity, unit) < 100.0) {
      az_vpluseq(&state->ship.velocity, az_vmul(unit, 800.0));
    }
  }
  switch (baddie->state) {
    case INITIAL_STATE:
      baddie->temp_properties |= AZ_BADF_INVINCIBLE;
      break;
    case START_DORMANCY_STATE:
      baddie->temp_properties |= AZ_BADF_INVINCIBLE;
      baddie->cooldown = 10.0;
      baddie->state = DORMANT_STATE;
      break;
    case DORMANT_STATE:
      baddie->temp_properties |= AZ_BADF_INVINCIBLE;
      if (baddie->cooldown <= 0.0) {
        start_charging_beam(state, baddie);
      }
      break;
    case CHARGE_BEAM_FORWARD_STATE:
      charge_rainbow_beam(state, baddie, time, 0, FIRE_BEAM_FORWARD_STATE);
      break;
    case CHARGE_BEAM_BACKWARD_STATE:
      charge_rainbow_beam(state, baddie, time, AZ_PI,
                          FIRE_BEAM_BACKWARD_STATE);
      break;
    case FIRE_BEAM_FORWARD_STATE:
      fire_rainbow_beam(state, baddie, time, 0);
      break;
    case FIRE_BEAM_BACKWARD_STATE:
      fire_rainbow_beam(state, baddie, time, AZ_PI);
      break;
    case PILLBOX_STATE:
      fire_pillbox_rockets(state, baddie, time);
      break;
    case PRISMATIC_STATE:
      fire_prismatic_walls(state, baddie, time);
      break;
    default:
      baddie->state = DORMANT_STATE;
      break;
  }
}

/*===========================================================================*/
