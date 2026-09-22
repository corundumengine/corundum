// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corundum::world {

  /**
   * @brief An NPC to spawn on a map at load time.
   *
   * col/row are integer tile coordinates. Spawn converts them to a fractional tile-space
   * Position, offset by the chunk origin for actors streamed from a world-mode chunk file.
   */
  struct Actor {
    int col = 0;
    int row = 0;

    std::string sprite_name;

    std::string dialogue_ref;     ///< Empty if the actor has no dialogue.
    std::string facing = "south"; ///< Direction the NPC faces; must name a core::Direction.

    std::string id; ///< Stable authoring id (optional; unique within one spawn-points file, key for persistence).
  };

  /**
   * @brief Optional per-map player placement from a spawn-points file.
   *
   * col/row are fractional tile coordinates, allowing sub-tile placement unlike Actor.
   */
  struct PlayerSpawn {
    float col = 0.f;
    float row = 0.f;
  };

  /** @brief Contents of a spawn-points JSON: NPCs plus optional player placement. */
  struct SpawnPoints {
    std::vector<Actor> actors;
    std::optional<PlayerSpawn> player;
  };

  /**
   * @brief Load spawn points from a JSON file.
   *
   * Expects an object with an "actors" array and an optional "player" object.
   * Returns a SpawnPoints with empty actors and no player if the file does not exist.
   *
   * @param path Path to the spawn points JSON (e.g. "data/spawn_points/world.json").
   * @return Loaded spawn points, or std::unexpected with an error description on failure.
   */
  [[nodiscard]] std::expected<SpawnPoints, std::string> load_spawn_points(const std::filesystem::path &path);

} // namespace corundum::world
