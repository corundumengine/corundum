// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/world/flags.hpp>

#include <expected>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace corundum {
  struct Engine;
}

namespace corundum::save {

  /** @brief Current on-disk save format version. */
  constexpr int k_save_version = 1;

  /** @brief JSON `mode` value for a single loaded tilemap. */
  constexpr std::string_view k_mode_single_map = "single_map";

  /** @brief JSON `mode` value for streamed world mode. */
  constexpr std::string_view k_mode_world = "world";

  /** @brief Everything serialized by a save: world/quest state plus spawn context.
   *
   * The whole global FlagStore is stored verbatim, including `zone.<id>.*` and
   * `npc.<id>.*` keys, so unknown author flags survive a round trip unchanged.
   * Anything that must survive a save (inventory, quest progress) therefore has
   * to be mirrored into the FlagStore; the record captures nothing else.
   */
  struct SaveState {
    int version = k_save_version; ///< On-disk format version.

    std::string game_id; ///< From game.json — guards cross-game loads.

    std::string mode = std::string{k_mode_single_map}; ///< k_mode_single_map or k_mode_world.

    std::string map_or_world_id; ///< Tilemap path (single_map) or world manifest path (world).

    std::string active_zone; ///< Scene::zone_id at save time.

    float player_col = 0.f; ///< Player tile column.

    float player_row = 0.f; ///< Player tile row.

    bool entered_from_world = false; ///< Return-journey marker across interiors.

    corundum::world::FlagStore flags; ///< Global flags, verbatim.
  };

  /** @brief Serialize a SaveState to JSON.
   *  @param state The state to serialize.
   *  @return The JSON document; save_game() writes it via core::write_json, which
   *          sorts keys for stable diffs. */
  [[nodiscard]] nlohmann::json to_json(const SaveState &state);

  /** @brief Deserialize a SaveState from JSON.
   *
   * Refuses a version newer than k_save_version with a clear message, runs the
   * migration chain for older versions, and applies defaults to any missing
   * field. Unknown flag keys are preserved verbatim.
   *
   * @param j The JSON document produced by to_json (or an older format).
   * @return The deserialized state, or an error message.
   */
  [[nodiscard]] std::expected<SaveState, std::string> from_json(const nlohmann::json &j);

  /** @brief Migrate a save JSON document in place from @p from_version to the current format.
   *
   * No migrations exist yet — version 1 is both the legacy (absent-field) format
   * and the current format, so this is a no-op today. Future steps are appended
   * in order and never edited once shipped.
   *
   * @param j            The save document to rewrite in place.
   * @param from_version The version the document was saved at.
   * @return ok, or an error for an invalid from_version.
   */
  [[nodiscard]] std::expected<void, std::string> migrate(nlohmann::json &j, int from_version);

  /** @brief Write the engine's current state to a save file.
   *
   * Captures the mode, active map/world, zone, player position, return-journey
   * marker, and the whole global FlagStore. The engine never auto-saves; game
   * code calls this when the player saves.
   *
   * @param engine The initialized engine to snapshot.
   * @param path   Destination file.
   * @return ok, or an error if the engine has no active map or the file cannot be written.
   */
  [[nodiscard]] std::expected<void, std::string> save_game(const Engine &engine, const std::filesystem::path &path);

  /** @brief Load a save file, replacing flags and respawning at the saved location.
   *
   * Loads the saved spawn, then replaces engine.flags and the return-journey
   * marker, driving the existing transition machinery (render::load_map /
   * world::enter_world) to rebuild the scene at the saved location. NPCs respawn
   * from data; their state returns from `npc.<id>.*` flags.
   *
   * @param engine The initialized engine to overwrite.
   * @param path   Save file to read.
   * @return ok, or an error if the file is missing, malformed, a newer format,
   *         for a different game_id or world, or the map/world cannot be loaded.
   * @post On failure the engine is left untouched; flags and scene are only
   *       replaced once the new scene has loaded successfully.
   */
  [[nodiscard]] std::expected<void, std::string> load_game(Engine &engine, const std::filesystem::path &path);

} // namespace corundum::save
