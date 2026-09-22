// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <expected>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace corundum::world {

  /// Current schema version for portals JSON. Absent field is treated as version 1.
  constexpr int k_portals_schema_version = 1;

  /**
   * @brief A one-way map transition trigger in tile-grid space.
   *
   * Defines a rectangular region on the current map that, when overlapped by the
   * player, initiates a transition to @p target_map, spawning the player at the
   * tile coordinate (@p spawn_col, @p spawn_row) in that map.
   *
   * Stored in the same tile-grid units as collision rects (see
   * corundum::world::tilemap::CollisionRect) rather than world pixels, so
   * the physics system can test the player's AABB against it directly — no
   * per-frame conversion round trip needed.
   */
  struct Portal {
    float col = 0.f;              ///< Left tile column of the trigger rect.
    float row = 0.f;              ///< Top tile row of the trigger rect.
    float w = 0.f;                ///< Width of trigger rect in tiles.
    float h = 0.f;                ///< Height of trigger rect in tiles.
    std::string target_map{};     ///< Path to the target tilemap JSON (empty for chunk-to-chunk portals).
    int spawn_col = 0;            ///< Tile column for player spawn in the target map or chunk.
    int spawn_row = 0;            ///< Tile row for player spawn in the target map or chunk.
    int target_chunk_col = -1;    ///< World-mode target chunk column (-1 = single-map portal).
    int target_chunk_row = -1;    ///< World-mode target chunk row.
    bool return_to_world = false; ///< Exit the current interior back to the configured overworld.
  };

  /**
   * @brief Pending map transition request produced when the player steps on a portal.
   *
   * Written into @c scene.pending_transition by the update system; consumed and
   * cleared by corundum::world::handle_map_transition.
   */
  struct MapTransition {
    std::string target_map{};     ///< Path to the target tilemap JSON.
    int spawn_col = 0;            ///< Tile column for player spawn in the target map.
    int spawn_row = 0;            ///< Tile row for player spawn in the target map.
    bool return_to_world = false; ///< Re-load the configured overworld instead of a named map.
  };

  /**
   * @brief Load portals from a portal JSON file.
   *
   * Expects an object with a "portals" array, each entry giving a tile-grid rect
   * (col, row, w, h) — the same coordinate space tilesmith authors and saves them
   * in, so no conversion happens at load time. An optional "schema_version" field
   * is read and migrated before the entries are parsed; an absent field is treated
   * as version 1. The legacy "target_chunk_x"/"target_chunk_y" spellings are still
   * accepted and normalised onto target_chunk_col/target_chunk_row.
   *
   * @param path Path to the portals JSON (e.g. "data/portals/world.json").
   * @return Loaded portals in tile-grid space (empty when @p path is absent or
   *         declares none), or std::unexpected with an error description on open,
   *         schema version, or parse failure.
   * @note A file that exists but cannot be opened is reported as an error rather
   *       than silently treated as absent.
   */
  [[nodiscard]] std::expected<std::vector<Portal>, std::string> load_portals(const std::filesystem::path &path);

  /** @brief Serialize a list of portals to JSON matching the portals file format.
   *
   * Emits the current @ref k_portals_schema_version and only writes the optional
   * target_chunk_* and return_to_world keys when they are actually set.
   *
   * @param[in] portals  The portals to serialize.
   * @return JSON object with a "portals" array, suitable for write_json().
   * @note The tile-grid rect fields are emitted as integers, so a fractional
   *       col/row/w/h would be truncated.
   */
  [[nodiscard]] nlohmann::json serialize(const std::vector<Portal> &portals);

} // namespace corundum::world
