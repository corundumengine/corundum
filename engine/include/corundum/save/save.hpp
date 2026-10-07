// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/world/flags.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum {
  struct Engine;
}

namespace corundum::core {
  struct GameConfig;
}

namespace corundum::save {

  /** @brief Current on-disk save format version. */
  constexpr int k_save_version = 1;

  /** @brief Number of manual save slots (`slot_01` … `slot_10`). */
  constexpr int k_manual_slot_count = 10;

  /** @brief Slot id of the framework-written autosave; the game decides when it happens. */
  constexpr std::string_view k_autosave_slot = "autosave";

  /** @brief Slot id of the F5/F9 quicksave. */
  constexpr std::string_view k_quicksave_slot = "quicksave";

  /** @brief Human-readable metadata shown in the save/load list.
   *
   *  The runtime cannot name gameplay types, so location_name is resolved by the caller
   *  (Gameplay maps the scene's zone id through its location registry). */
  struct SaveMeta {
    std::string location_name{}; ///< Display name of the location at save time.

    std::int64_t playtime_seconds{}; ///< Accumulated playtime, in seconds.

    std::int64_t saved_at_unix{}; ///< Wall-clock save time, as Unix epoch seconds.
  };

  /** @brief One row of the save/load list: a slot on disk, or the error that makes it corrupt.
   *
   *  @c meta is set only for a valid, loadable slot; @c error is set only for a corrupt one.
   *  A slot whose file does not exist produces no row at all. */
  struct SaveSlotInfo {
    std::filesystem::path path{}; ///< Absolute path of the slot file.

    std::string slot_id{}; ///< File stem: `slot_01`, `quicksave` or `autosave`.

    std::optional<SaveMeta> meta{}; ///< Metadata when the slot is valid; empty when corrupt.

    std::string error{}; ///< Why the slot is corrupt; empty when valid.
  };

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

    SaveMeta meta{}; ///< Slot-list metadata (location name, playtime, timestamp).

    corundum::world::FlagStore flags; ///< Global flags, verbatim.
  };

  /** @brief The file name for manual slot @p index (0-based), e.g. `slot_01`.
   *  @pre 0 <= index < k_manual_slot_count. */
  [[nodiscard]] std::string manual_slot_id(int index);

  /** @brief The path of slot @p slot_id inside @p directory (`<slot_id>.json`). */
  [[nodiscard]] std::filesystem::path slot_path(const std::filesystem::path &directory, std::string_view slot_id);

  /** @brief `<user data dir game_id>/saves`, the directory all save slots live in.
   *  @param cfg Supplies game_id; an empty game_id is an error (see core::user_data_dir).
   *  @return The (not necessarily existing) saves directory, or an error message. */
  [[nodiscard]] std::expected<std::filesystem::path, std::string> saves_directory(const core::GameConfig &cfg);

  /** @brief List the save slots that exist in @p directory.
   *
   *  Returns one row per existing manual slot, plus @c autosave and @c quicksave when present,
   *  ordered autosave, quicksave, then `slot_01` … `slot_10`. A slot file that fails to parse,
   *  declares a different format version, or was written for a @p game_id other than the
   *  current one becomes a row with @c error set and @c meta empty; a missing file produces no
   *  row.
   *
   *  @param directory Directory holding the slot files (see saves_directory).
   *  @param game_id   Current game's id, used to reject cross-game saves.
   */
  [[nodiscard]] std::vector<SaveSlotInfo> list_saves(const std::filesystem::path &directory, std::string_view game_id);

  /** @brief Serialize a SaveState to JSON.
   *  @param state The state to serialize.
   *  @return The JSON document; save_game() writes it via core::write_json, which
   *          sorts keys for stable diffs. */
  [[nodiscard]] nlohmann::json serialize(const SaveState &state);

  /** @brief Deserialize a SaveState from JSON.
   *
   * Refuses any version but k_save_version, and applies defaults to any missing
   * field. Unknown flag keys are preserved verbatim.
   *
   * @param j The JSON document produced by serialize.
   * @return The deserialized state, or an error message.
   */
  [[nodiscard]] std::expected<SaveState, std::string> parse(const nlohmann::json &j);

  /** @brief Write the engine's current state to a save file.
   *
   * Captures the mode, active map/world, zone, player position, return-journey
   * marker, and the whole global FlagStore. The engine never auto-saves; game
   * code calls this when the player saves.
   *
   * @param engine          The initialized engine to snapshot.
   * @param path            Destination file; its parent directory is created if missing.
   * @param location_name   Display name of the current location, for the slot list.
   * @param playtime_seconds Accumulated playtime to record, in seconds.
   * @return ok, or an error if the engine has no active map or the file cannot be written.
   * @post The slot's saved_at_unix is stamped with the current wall-clock time.
   */
  [[nodiscard]] std::expected<void, std::string> save_game(const Engine &engine, const std::filesystem::path &path,
                                                           std::string_view location_name,
                                                           std::int64_t playtime_seconds);

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
