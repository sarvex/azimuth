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

#include "azimuth/state/baddie.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h> // for NULL
#include <string.h> // for memset

#include "azimuth/state/pickup.h" // for AZ_PUPF_* macros
#include "azimuth/util/misc.h"
#include "azimuth/util/polygon.h"
#include "azimuth/util/vector.h"

/*===========================================================================*/

#define DECL_COMPONENTS(c) .num_components=AZ_ARRAY_SIZE(c), .components=(c)

static const az_vector_t lump_vertices[] = {
  {20, 0}, {15, 15}, {-15, 15}, {-15, -15}, {15, -15}
};

static const az_vector_t turret_vertices[] = {
  {20, 0}, {10, 17.320508075688775}, {-10, 17.320508075688775},
  {-20, 0}, {-10, -17.320508075688775}, {10, -17.320508075688775}
};
static const az_vector_t turret_cannon_vertices[] = {
  {30, 5}, {0, 5}, {0, -5}, {30, -5}
};
static az_component_data_t turret_components[] = {
  { .polygon = AZ_INIT_POLYGON(turret_cannon_vertices),
    .immunities = AZ_DMGF_NORMAL }
};

static const az_vector_t zipper_vertices[] = {
  {20, 0}, {5, 10}, {-15, 5}, {-15, -5}, {5, -10}
};

static az_component_data_t atom_components[] = {
  { .bounding_radius = 6.3, .immunities = ~AZ_DMGF_MEGA_BOMB },
  { .bounding_radius = 6.5, .immunities = ~AZ_DMGF_MEGA_BOMB },
  { .bounding_radius = 6.7, .immunities = ~AZ_DMGF_MEGA_BOMB }
};

static const az_vector_t box_vertices[] = {
  {16, 11}, {11, 16}, {-11, 16}, {-16, 11},
  {-16, -11}, {-11, -16}, {11, -16}, {16, -11}
};

static az_baddie_data_t baddie_datas[] = {
  [AZ_BAD_LUMP] = {
    .max_health = 10.0,
    .potential_pickups = (AZ_PUPF_NOTHING | AZ_PUPF_SMALL_SHIELDS),
    .main_body = { .polygon = AZ_INIT_POLYGON(lump_vertices) }
  },
  [AZ_BAD_TURRET] = {
    .overall_bounding_radius = 30.5,
    .max_health = 15.0,
    .potential_pickups = (AZ_PUPF_NOTHING | AZ_PUPF_SMALL_SHIELDS |
                          AZ_PUPF_ROCKETS),
    .main_body = { .polygon = AZ_INIT_POLYGON(turret_vertices),
                   .immunities = AZ_DMGF_NORMAL },
    DECL_COMPONENTS(turret_components)
  },
  [AZ_BAD_ZIPPER] = {
    .max_health = 20.0,
    .potential_pickups = AZ_PUPF_ALL,
    .main_body = { .polygon = AZ_INIT_POLYGON(zipper_vertices) }
  },
  [AZ_BAD_BOUNCER] = {
    .max_health = 5.0,
    .potential_pickups = AZ_PUPF_ALL,
    .main_body = { .bounding_radius = 15.0 }
  },
  [AZ_BAD_ATOM] = {
    .overall_bounding_radius = 40.0,
    .max_health = 15.0,
    .potential_pickups = AZ_PUPF_ALL,
    .main_body = { .bounding_radius = 10.0 },
    DECL_COMPONENTS(atom_components)
  },
  [AZ_BAD_SPINER] = {
    .max_health = 10.0,
    .potential_pickups = AZ_PUPF_ALL,
    .main_body = { .bounding_radius = 18.0 }
  },
  [AZ_BAD_BOX] = {
    .max_health = 10.0,
    .potential_pickups = ~(AZ_PUPF_NOTHING | AZ_PUPF_LARGE_SHIELDS),
    .main_body = { .polygon = AZ_INIT_POLYGON(box_vertices),
                   .immunities = AZ_DMGF_FREEZE }
  },
  [AZ_BAD_ARMORED_BOX] = {
    .max_health = 20.0,
    .potential_pickups = ~(AZ_PUPF_NOTHING | AZ_PUPF_SMALL_SHIELDS),
    .main_body = { .polygon = AZ_INIT_POLYGON(box_vertices),
                   .immunities = (AZ_DMGF_NORMAL | AZ_DMGF_CHARGED |
                                  AZ_DMGF_FREEZE | AZ_DMGF_PIERCE) }
  }
};

AZ_STATIC_ASSERT(AZ_ARRAY_SIZE(baddie_datas) == AZ_NUM_BADDIE_KINDS + 1);

/*===========================================================================*/

static double polygon_bounding_radius(az_polygon_t polygon) {
  double radius = 0.0;
  for (int i = 0; i < polygon.num_vertices; ++i) {
    radius = fmax(radius, az_vnorm(polygon.vertices[i]));
  }
  return radius + 0.01; // small safety margin
}

static void init_component_data(az_component_data_t *component) {
  if (component->polygon.num_vertices > 0) {
    assert(component->bounding_radius == 0.0);
    component->bounding_radius =
      polygon_bounding_radius(component->polygon);
  } else assert(component->bounding_radius > 0.0);
}

static bool baddie_data_initialized = false;

void az_init_baddie_datas(void) {
  assert(!baddie_data_initialized);
  bool skipped = false;
  AZ_ARRAY_LOOP(data, baddie_datas) {
    // Skip the first entry in the array (which is for AZ_BAD_NOTHING)
    if (!skipped) {
      skipped = true;
      continue;
    }
    // Set bounding radius for all components.
    for (int i = 0; i < data->num_components; ++i) {
      // N.B. We need to cast away the const-ness of the data->components
      // pointer here, so that we can initialize its bounding_radius.  We could
      // avoid needing a const-cast by accessing the component data arrays
      // directly by name (as they are each declared non-const above), but it's
      // far more convenient to go through the data->components pointer.
      az_component_data_t *component =
        (az_component_data_t *)(&data->components[i]);
      init_component_data(component);
    }
    // Set main body bounding radius.
    init_component_data(&data->main_body);
    // Set overall bounding radius.
    if (data->num_components == 0) {
      assert(data->overall_bounding_radius == 0.0);
      data->overall_bounding_radius = data->main_body.bounding_radius;
    }
    // Sanity-check the overall bounding radius.
    assert(data->overall_bounding_radius >= data->main_body.bounding_radius);
    for (int i = 0; i < data->num_components; ++i) {
      assert(data->overall_bounding_radius >=
             data->components[i].bounding_radius);
    }
    // Sanity-check other fields.
    assert(data->max_health > 0.0);
  }
  baddie_data_initialized = true;
}

const az_baddie_data_t *az_get_baddie_data(az_baddie_kind_t kind) {
  assert(baddie_data_initialized);
  assert(kind != AZ_BAD_NOTHING);
  const int data_index = (int)kind;
  assert(0 <= data_index && data_index < AZ_ARRAY_SIZE(baddie_datas));
  return &baddie_datas[data_index];
}

void az_init_baddie(az_baddie_t *baddie, az_baddie_kind_t kind,
                    az_vector_t position, double angle) {
  assert(kind != AZ_BAD_NOTHING);
  const az_uid_t uid = baddie->uid;
  assert(uid != AZ_NULL_UID);
  assert(uid != AZ_SHIP_UID);
  memset(baddie, 0, sizeof(*baddie));
  baddie->kind = kind;
  baddie->data = az_get_baddie_data(kind);
  baddie->uid = uid;
  baddie->position = position;
  baddie->angle = angle;
  baddie->health = baddie->data->max_health;
  switch (kind) {
    case AZ_BAD_ATOM:
      for (int i = 0; i < baddie->data->num_components; ++i) {
        baddie->components[i].angle = i * AZ_DEG2RAD(100);
      }
      break;
    default: break;
  }
}

/*===========================================================================*/

static bool ray_hits_component(
    const az_component_data_t *component, az_vector_t position, double angle,
    az_vector_t start, az_vector_t delta, az_vector_t *point_out,
    az_vector_t *normal_out) {
  if (component->polygon.num_vertices > 0) {
    return (az_ray_hits_bounding_circle(start, delta, position,
                                        component->bounding_radius) &&
            az_ray_hits_polygon_trans(component->polygon, position,
                                      angle, start, delta,
                                      point_out, normal_out));
  } else {
    return az_ray_hits_circle(component->bounding_radius, position,
                              start, delta, point_out, normal_out);
  }
}

bool az_ray_hits_baddie(
    const az_baddie_t *baddie, az_vector_t start, az_vector_t delta,
    az_vector_t *point_out, az_vector_t *normal_out,
    const az_component_data_t **component_out) {
  assert(baddie->kind != AZ_BAD_NOTHING);
  const az_baddie_data_t *data = baddie->data;

  // Common case: if ray definitely misses baddie, return early.
  if (!az_ray_hits_bounding_circle(start, delta, baddie->position,
                                   data->overall_bounding_radius)) {
    return false;
  }

  // Calculate start and delta relative to the positioning of the baddie.
  const az_vector_t rel_start = az_vrotate(az_vsub(start, baddie->position),
                                           -baddie->angle);
  az_vector_t rel_delta = az_vrotate(delta, -baddie->angle);
  const az_component_data_t *hit_component = NULL;
  az_vector_t point = AZ_VZERO;

  // Check if we hit the main body of the baddie.
  if (ray_hits_component(&data->main_body, AZ_VZERO, 0.0, rel_start,
                         rel_delta, &point, normal_out)) {
    hit_component = &data->main_body;
    rel_delta = az_vsub(point, rel_start);
  }

  // Now check if we hit any of the baddie's components.
  for (int i = 0; i < data->num_components; ++i) {
    assert(i < AZ_ARRAY_SIZE(baddie->components));
    const az_component_data_t *component = &data->components[i];
    if (ray_hits_component(component, baddie->components[i].position,
                           baddie->components[i].angle, rel_start, rel_delta,
                           &point, normal_out)) {
      hit_component = component;
      rel_delta = az_vsub(point, rel_start);
    }
  }

  // Fix up *point_out and *normal_out and return.
  if (hit_component != NULL) {
    if (point_out != NULL) {
      *point_out = az_vadd(az_vrotate(point, baddie->angle), baddie->position);
    }
    if (normal_out != NULL) {
      *normal_out = az_vrotate(*normal_out, baddie->angle);
    }
    if (component_out != NULL) *component_out = hit_component;
    return true;
  }
  return false;
}

/*===========================================================================*/

static bool circle_hits_component(
    const az_component_data_t *component, az_vector_t position, double angle,
    double radius, az_vector_t start, az_vector_t delta,
    az_vector_t *pos_out, az_vector_t *impact_out) {
  if (component->polygon.num_vertices > 0) {
    return (az_ray_hits_bounding_circle(start, delta, position,
                                        component->bounding_radius + radius) &&
            az_circle_hits_polygon_trans(component->polygon, position, angle,
                                         radius, start, delta,
                                         pos_out, impact_out));
  } else {
    return az_circle_hits_circle(component->bounding_radius, position,
                                 radius, start, delta, pos_out, impact_out);
  }
}

bool az_circle_hits_baddie(
    const az_baddie_t *baddie, double radius, az_vector_t start,
    az_vector_t delta, az_vector_t *pos_out, az_vector_t *impact_out,
    const az_component_data_t **component_out) {
  assert(baddie->kind != AZ_BAD_NOTHING);
  const az_baddie_data_t *data = baddie->data;

  // Common case: if circle definitely misses baddie, return early.
  if (!az_ray_hits_bounding_circle(start, delta, baddie->position,
                                   data->overall_bounding_radius + radius)) {
    return false;
  }

  // Calculate start and delta relative to the positioning of the baddie.
  const az_vector_t rel_start = az_vrotate(az_vsub(start, baddie->position),
                                           -baddie->angle);
  az_vector_t rel_delta = az_vrotate(delta, -baddie->angle);
  const az_component_data_t *hit_component = NULL;
  az_vector_t pos = AZ_VZERO;

  // Check if we hit the main body of the baddie.
  if (circle_hits_component(&data->main_body, AZ_VZERO, 0.0, radius, rel_start,
                            rel_delta, &pos, impact_out)) {
    hit_component = &data->main_body;
    rel_delta = az_vsub(pos, rel_start);
  }

  // Now check if we hit any of the baddie's components.
  for (int i = 0; i < data->num_components; ++i) {
    assert(i < AZ_ARRAY_SIZE(baddie->components));
    const az_component_data_t *component = &data->components[i];
    if (circle_hits_component(component, baddie->components[i].position,
                              baddie->components[i].angle, radius, rel_start,
                              rel_delta, &pos, impact_out)) {
      hit_component = component;
      rel_delta = az_vsub(pos, rel_start);
    }
  }

  // Fix up *pos_out and *impact_out and return.
  if (hit_component != NULL) {
    if (pos_out != NULL) {
      *pos_out = az_vadd(az_vrotate(pos, baddie->angle), baddie->position);
    }
    if (impact_out != NULL) {
      *impact_out = az_vadd(az_vrotate(*impact_out, baddie->angle),
                            baddie->position);
    }
    if (component_out != NULL) *component_out = hit_component;
    return true;
  }
  return false;
}

/*===========================================================================*/
