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

#include "azimuth/state/save.h"

#include <assert.h>
#include <inttypes.h> // for PRIx64 and SCNx64
#include <stdbool.h>
#include <stdio.h>

#include "azimuth/state/planet.h"
#include "azimuth/state/player.h"
#include "azimuth/state/upgrade.h"
#include "azimuth/util/misc.h"

/*===========================================================================*/

void az_reset_saved_games(az_saved_games_t *games) {
  AZ_ARRAY_LOOP(game, games->games) game->present = false;
}

/*===========================================================================*/

static bool parse_bitfield(FILE *file, uint64_t *array, int array_length) {
  assert(array_length >= 1);
  if (fscanf(file, "=%"SCNx64, &array[0]) < 1) return false;
  for (int i = 1; i < array_length; ++i) {
    if (fscanf(file, ":%"SCNx64, &array[i]) < 1) return false;
  }
  return true;
}

#define READ_BITFIELD(prefix, array) do { \
    if (fscanf(file, prefix) < 0) return false; \
    if (!parse_bitfield(file, (array), AZ_ARRAY_SIZE(array))) { \
      return false; \
    } \
  } while (0)

static bool parse_saved_game(const az_planet_t *planet, FILE *file,
                             az_player_t *player) {
  az_init_player(player);

  uint64_t upgrades[AZ_ARRAY_SIZE(player->upgrades)];
  READ_BITFIELD(" up", upgrades);
  READ_BITFIELD(" rv", player->rooms_visited);
  READ_BITFIELD(" zm", player->zones_mapped);
  READ_BITFIELD(" fl", player->flags);
  int rockets, bombs, gun1, gun2, ordnance;
  if (fscanf(file, " tt=%lf cr=%d rk=%d bm=%d g1=%d g2=%d or=%d\n",
             &player->total_time, &player->current_room, &rockets, &bombs,
             &gun1, &gun2, &ordnance) < 7) return false;
  if (player->total_time < 0.0) return false;
  if (player->current_room < 0 ||
      player->current_room >= planet->num_rooms) {
    return false;
  }

  // Grant upgrades to the player.  This will correctly set their maximum
  // shields/energy/rockets/bombs.
  for (int i = 0; i < AZ_ARRAY_SIZE(upgrades); ++i) {
    for (int j = 0; j < 64; ++j) {
      const int index = 64 * i + j;
      if (index >= AZ_NUM_UPGRADES) break;
      if ((upgrades[i] & (UINT64_C(1) << j)) != 0) {
        az_give_upgrade(player, (az_upgrade_t)index);
      }
    }
  }
  // Validate and set current ammo stock (rockets and bombs).
  if (rockets < 0 || rockets > player->max_rockets) return false;
  else player->rockets = rockets;
  if (bombs < 0 || bombs > player->max_bombs) return false;
  else player->bombs = bombs;
  // Save points recharge energy and shields, so set shields and energy to
  // their maximums.
  player->shields = player->max_shields;
  player->energy = player->max_energy;
  // Validate and select guns.
  if (gun1 < (int)AZ_GUN_NONE || gun1 > (int)AZ_GUN_BEAM ||
      gun2 < (int)AZ_GUN_NONE || gun2 > (int)AZ_GUN_BEAM) return false;
  az_select_gun(player, (az_gun_t)gun1);
  az_select_gun(player, (az_gun_t)gun2);
  // Validate and select ordnance.
  if (ordnance < (int)AZ_ORDN_NONE || ordnance > (int)AZ_ORDN_BOMBS) {
    return false;
  }
  az_select_ordnance(player, (az_ordnance_t)ordnance);
  return true;
}

#undef READ_BITFIELD

static bool parse_saved_games(const az_planet_t *planet, FILE *file,
                              az_saved_games_t *games_out) {
  AZ_ARRAY_LOOP(game, games_out->games) {
    if (fgetc(file) != '@') return false;
    switch (fgetc(file)) {
      case 'S':
        game->present = true;
        if (!parse_saved_game(planet, file, &game->player)) return false;
        break;
      case 'N':
        game->present = false;
        if (fscanf(file, "\n") < 0) return false;
        break;
      default: return false;
    }
  }
  return true;
}

bool az_load_games_from_file(const az_planet_t *planet,
                             const char *filepath,
                             az_saved_games_t *games_out) {
  assert(games_out != NULL);
  FILE *file = fopen(filepath, "r");
  if (file == NULL) return false;
  const bool ok = parse_saved_games(planet, file, games_out);
  fclose(file);
  return ok;
}

/*===========================================================================*/

static bool write_bitfield(const char *prefix, const uint64_t *array,
                           int array_length, FILE *file) {
  assert(array_length >= 1);
  if (fprintf(file, " %s=%"PRIx64, prefix, array[0]) < 0) return false;
  for (int i = 1; i < array_length; ++i) {
    if (fprintf(file, ":%"PRIx64, array[i]) < 0) return false;
  }
  return true;
}

#define WRITE_BITFIELD(prefix, array) do { \
    if (!write_bitfield(prefix, array, AZ_ARRAY_SIZE(array), file)) { \
      return false; \
    } \
  } while (0)

static bool write_games(const az_saved_games_t *games, FILE *file) {
  AZ_ARRAY_LOOP(game, games->games) {
    if (game->present) {
      const az_player_t *player = &game->player;
      if (fprintf(file, "@S") < 0) return false;
      WRITE_BITFIELD("up", player->upgrades);
      WRITE_BITFIELD("rv", player->rooms_visited);
      WRITE_BITFIELD("zm", player->zones_mapped);
      WRITE_BITFIELD("fl", player->flags);
      if (fprintf(file, " tt=%.02f cr=%d rk=%d bm=%d g1=%d g2=%d or=%d\n",
                  player->total_time, player->current_room,
                  player->rockets, player->bombs, player->gun1, player->gun2,
                  player->ordnance) < 0) return false;
    } else {
      if (fprintf(file, "@N\n") < 0) return false;
    }
  }
  return true;
}

#undef WRITE_BITFIELD

bool az_save_games_to_file(const az_saved_games_t *games,
                           const char *filepath) {
  assert(games != NULL);
  FILE *file = fopen(filepath, "w");
  if (file == NULL) return false;
  const bool ok = write_games(games, file);
  fclose(file);
  return ok;
}

/*===========================================================================*/
