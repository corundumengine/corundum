// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/render_state.hpp>
#include <corundum/save/save.hpp>
#include <corundum/world/flags.hpp>
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/engine.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/transition.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace corundum::save {

  namespace {

    /// Copy `j[key]` into @p out when present, rejecting a present value whose JSON
    /// type doesn't match @p T. Absent keys leave @p out at its existing default.
    template <typename T>
    [[nodiscard]] std::expected<void, std::string> read_field(const nlohmann::json &j, std::string_view key, T &out) {
      const auto it = j.find(key);
      if (it == j.end())
        return {};

      if constexpr (std::is_same_v<T, std::string>) {
        if (!it->is_string())
          return std::unexpected(std::format("save '{}' must be a string", key));
      } else if constexpr (std::is_same_v<T, bool>) {
        if (!it->is_boolean())
          return std::unexpected(std::format("save '{}' must be a boolean", key));
      } else {
        if (!it->is_number())
          return std::unexpected(std::format("save '{}' must be a number", key));
      }

      out = it->get<T>();
      return {};
    }

    /// Serialize the whole FlagStore as a `{ "key": int }` object. Written
    /// manually (not via nlohmann's map support) so no conversion surprises
    /// depend on which container flat_map aliases.
    [[nodiscard]] nlohmann::json serialize_flags(const corundum::world::FlagStore &flags) {
      nlohmann::json j = nlohmann::json::object();
      for (const auto &[key, value] : flags)
        j[key] = value;
      return j;
    }

  } // namespace

  nlohmann::json to_json(const SaveState &state) {
    nlohmann::json j;
    j["version"] = state.version;
    j["game_id"] = state.game_id;
    j["mode"] = state.mode;
    j["map_or_world_id"] = state.map_or_world_id;
    j["active_zone"] = state.active_zone;
    j["player_col"] = state.player_col;
    j["player_row"] = state.player_row;
    j["entered_from_world"] = state.entered_from_world;
    j["flags"] = serialize_flags(state.flags);
    return j;
  }

  std::expected<void, std::string> migrate(nlohmann::json & /*j*/, int from_version) {
    if (from_version < 1)
      return std::unexpected(std::format("save has invalid version {}", from_version));
    // Future versions append migration steps here, in order:
    //   if (from_version < 2) { /* rewrite v1 fields into v2 shape */ from_version = 2; }
    // Existing steps must never be edited once shipped, since already-migrated
    // files may depend on the exact transformation a step performed.
    return {};
  }

  std::expected<SaveState, std::string> from_json(const nlohmann::json &j) {
    if (!j.is_object())
      return std::unexpected("save JSON must be an object");

    if (j.contains("version") && !j["version"].is_number_integer())
      return std::unexpected("save 'version' must be an integer");
    const int version = j.value("version", 1);
    if (version > k_save_version)
      return std::unexpected(
          std::format("save version {} is newer than supported version {}", version, k_save_version));

    nlohmann::json migrated = j;
    if (auto result = migrate(migrated, version); !result)
      return std::unexpected(std::move(result).error());
    // When migrate() gains real steps, verify here that the document actually reached
    // k_save_version — a no-op migration would otherwise let an old save parse as the current
    // format. prepare_schema_version enforces this for asset documents; for save, decide whether
    // to check `migrated["version"]` (steps bump the field) or have migrate() return the version
    // it reached.

    SaveState s;
    s.version = k_save_version;

    if (auto result = read_field(migrated, "game_id", s.game_id); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "mode", s.mode); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "map_or_world_id", s.map_or_world_id); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "active_zone", s.active_zone); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "player_col", s.player_col); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "player_row", s.player_row); !result)
      return std::unexpected(std::move(result).error());
    if (auto result = read_field(migrated, "entered_from_world", s.entered_from_world); !result)
      return std::unexpected(std::move(result).error());

    if (s.mode != k_mode_single_map && s.mode != k_mode_world)
      return std::unexpected(std::format("save 'mode' must be '{}' or '{}'", k_mode_single_map, k_mode_world));

    if (migrated.contains("flags")) {
      if (!migrated["flags"].is_object())
        return std::unexpected("save 'flags' must be an object");
      for (const auto &[key, value] : migrated["flags"].items()) {
        if (!value.is_number_integer())
          return std::unexpected(std::format("save flag '{}' must be an integer", key));
        s.flags.emplace(key, value.get<int>());
      }
    }
    return s;
  }

  std::expected<void, std::string> save_game(const Engine &engine, const std::filesystem::path &path) {
    SaveState state;
    state.version = k_save_version;
    state.game_id = engine.cfg.game_id;
    state.mode =
        engine.render.mode == render::RenderMode::World ? std::string{k_mode_world} : std::string{k_mode_single_map};

    if (state.mode == k_mode_world) {
      state.map_or_world_id = engine.cfg.paths.world_manifest_path;
    } else {
      const auto *tm = render::active_tilemap(engine.render);
      if (tm == nullptr)
        return std::unexpected("save_game: no active tilemap in single_map mode");
      state.map_or_world_id = tm->path;
    }

    state.active_zone = engine.scene.zone_id;
    state.player_col = engine.scene.world.transforms.pos_col(engine.scene.player);
    state.player_row = engine.scene.world.transforms.pos_row(engine.scene.player);
    state.entered_from_world = engine.entered_from_world;
    state.flags = engine.flags;

    return core::write_json(path, to_json(state));
  }

  std::expected<void, std::string> load_game(Engine &engine, const std::filesystem::path &path) {
    auto json_result = core::read_json(path);
    if (!json_result)
      return std::unexpected(std::move(json_result).error());

    auto state = from_json(*json_result);
    if (!state)
      return std::unexpected(std::move(state).error());

    if (state->game_id != engine.cfg.game_id)
      return std::unexpected(
          std::format("save '{}' is for game '{}', not '{}'", path.string(), state->game_id, engine.cfg.game_id));

    if (state->mode == k_mode_world && !state->map_or_world_id.empty() &&
        state->map_or_world_id != engine.cfg.paths.world_manifest_path)
      return std::unexpected(std::format("save '{}' is for world '{}', not '{}'", path.string(), state->map_or_world_id,
                                         engine.cfg.paths.world_manifest_path));

    // Rebuild the scene first: apply_spawn reads no flag state, so committing flags
    // only after it succeeds keeps a failed load from leaving the engine with the
    // save's flags but the previous scene.
    const corundum::world::SpawnMode spawn_mode =
        state->mode == k_mode_world ? corundum::world::SpawnMode::World : corundum::world::SpawnMode::SingleMap;
    auto spawn = world::apply_spawn(engine, spawn_mode, state->map_or_world_id, state->active_zone, state->player_col,
                                    state->player_row);
    if (!spawn)
      return std::unexpected(std::move(spawn).error());

    engine.flags = std::move(state->flags);
    engine.entered_from_world = state->entered_from_world;
    return {};
  }

} // namespace corundum::save