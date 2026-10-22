// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/render_state.hpp>
#include <corundum/save/save.hpp>
#include <corundum/world/flags.hpp>
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/engine.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/transition.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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
      } else if constexpr (std::is_integral_v<T>) {
        if (!it->is_number_integer())
          return std::unexpected(std::format("save '{}' must be an integer", key));
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

    [[nodiscard]] nlohmann::json serialize_meta(const SaveMeta &meta) {
      return nlohmann::json{
          {"location_name", meta.location_name},
          {"playtime_seconds", meta.playtime_seconds},
          {"saved_at_unix", meta.saved_at_unix},
      };
    }

    /// Parse the optional `meta` object. Absent leaves @p out at its defaults; present must be an
    /// object with correctly-typed fields.
    [[nodiscard]] std::expected<void, std::string> parse_meta(const nlohmann::json &j, SaveMeta &out) {
      const auto it = j.find("meta");
      if (it == j.end())
        return {};
      if (!it->is_object())
        return std::unexpected("save 'meta' must be an object");
      if (auto result = read_field(*it, "location_name", out.location_name); !result)
        return result;
      if (auto result = read_field(*it, "playtime_seconds", out.playtime_seconds); !result)
        return result;
      if (auto result = read_field(*it, "saved_at_unix", out.saved_at_unix); !result)
        return result;
      return {};
    }

  } // namespace

  nlohmann::json serialize(const SaveState &state) {
    nlohmann::json j;
    j["version"] = state.version;
    j["game_id"] = state.game_id;
    j["mode"] = state.mode;
    j["map_or_world_id"] = state.map_or_world_id;
    j["active_zone"] = state.active_zone;
    j["player_col"] = state.player_col;
    j["player_row"] = state.player_row;
    j["entered_from_world"] = state.entered_from_world;
    j["meta"] = serialize_meta(state.meta);
    j["flags"] = serialize_flags(state.flags);
    return j;
  }

  namespace {

    std::expected<void, std::string> read_identity_fields(const nlohmann::json &j, SaveState &state) {
      if (auto result = read_field(j, "game_id", state.game_id); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "mode", state.mode); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "map_or_world_id", state.map_or_world_id); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "active_zone", state.active_zone); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "player_col", state.player_col); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "player_row", state.player_row); !result)
        return std::unexpected(std::move(result).error());
      if (auto result = read_field(j, "entered_from_world", state.entered_from_world); !result)
        return std::unexpected(std::move(result).error());
      return parse_meta(j, state.meta);
    }

    std::expected<void, std::string> read_flags(const nlohmann::json &j, corundum::world::FlagStore &flags) {
      if (!j.contains("flags"))
        return {};
      if (!j["flags"].is_object())
        return std::unexpected("save 'flags' must be an object");
      for (const auto &[key, value] : j["flags"].items()) {
        if (!value.is_number_integer())
          return std::unexpected(std::format("save flag '{}' must be an integer", key));
        flags.emplace(key, value.get<int>());
      }
      return {};
    }

  } // namespace

  std::expected<SaveState, std::string> parse(const nlohmann::json &j) {
    if (!j.is_object())
      return std::unexpected("save JSON must be an object");

    if (j.contains("version") && !j["version"].is_number_integer())
      return std::unexpected("save 'version' must be an integer");
    const int version = j.value("version", 1);
    if (version != k_save_version)
      return std::unexpected(std::format("save version {} is not supported (expected {})", version, k_save_version));

    SaveState s;
    s.version = k_save_version;

    if (auto result = read_identity_fields(j, s); !result)
      return std::unexpected(std::move(result).error());
    if (s.mode != k_mode_single_map && s.mode != k_mode_world)
      return std::unexpected(std::format("save 'mode' must be '{}' or '{}'", k_mode_single_map, k_mode_world));
    if (auto result = read_flags(j, s.flags); !result)
      return std::unexpected(std::move(result).error());
    return s;
  }

  std::expected<void, std::string> save_game(const Engine &engine, core::UserStorage &storage, std::string_view slot_id,
                                             std::string_view location_name, std::int64_t playtime_seconds) {
    SaveState state;
    state.version = k_save_version;
    state.game_id = engine.cfg.game_id;
    state.meta.location_name = std::string{location_name};
    state.meta.playtime_seconds = playtime_seconds;
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    state.meta.saved_at_unix = std::chrono::duration_cast<std::chrono::seconds>(now).count();
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

    if (!world::player_present(engine.scene))
      return std::unexpected("save_game: no player entity");

    state.player_col = engine.scene.world.transforms.pos_col(engine.scene.player);
    state.player_row = engine.scene.world.transforms.pos_row(engine.scene.player);
    state.entered_from_world = engine.entered_from_world;
    state.flags = engine.flags;

    return core::write_json(storage, slot_name(slot_id), serialize(state));
  }

  std::string manual_slot_id(int index) {
    return std::format("slot_{:02d}", index + 1);
  }

  std::string slot_name(std::string_view slot_id) {
    return std::format("saves/{}.json", slot_id);
  }

  std::vector<SaveSlotInfo> list_saves(const core::UserStorage &storage, std::string_view game_id) {
    std::vector<SaveSlotInfo> rows;

    const auto append = [&](std::string_view slot_id) {
      const std::string name = slot_name(slot_id);
      if (!storage.contains(name))
        return;

      SaveSlotInfo info;
      info.slot_id = std::string{slot_id};

      const std::expected<nlohmann::json, std::string> json = core::read_json(storage, name, "save");
      if (!json) {
        info.error = json.error();
      } else if (const std::expected<SaveState, std::string> state = parse(*json); !state) {
        info.error = state.error();
      } else if (state->game_id != game_id) {
        info.error = std::format("save is for game '{}', not '{}'", state->game_id, game_id);
      } else {
        info.meta = state->meta;
      }
      rows.push_back(std::move(info));
    };

    // Autosave and quicksave first, then the manual slots, in slot order.
    append(k_autosave_slot);
    append(k_quicksave_slot);
    for (int index = 0; index < k_manual_slot_count; ++index)
      append(manual_slot_id(index));

    return rows;
  }

  std::expected<SaveMeta, std::string> load_game(Engine &engine, const core::UserStorage &storage,
                                                 std::string_view slot_id) {
    auto json_result = core::read_json(storage, slot_name(slot_id));
    if (!json_result)
      return std::unexpected(std::move(json_result).error());

    auto state = parse(*json_result);
    if (!state)
      return std::unexpected(std::move(state).error());

    if (state->game_id != engine.cfg.game_id)
      return std::unexpected(
          std::format("save '{}' is for game '{}', not '{}'", slot_id, state->game_id, engine.cfg.game_id));

    if (state->mode == k_mode_world && !state->map_or_world_id.empty() &&
        state->map_or_world_id != engine.cfg.paths.world_manifest_path)
      return std::unexpected(std::format("save '{}' is for world '{}', not '{}'", slot_id, state->map_or_world_id,
                                         engine.cfg.paths.world_manifest_path));

    // Rebuild the scene first: apply_spawn reads no flag state, so committing flags
    // only after it succeeds keeps a failed load from leaving the engine with the
    // save's flags but the previous scene.
    const corundum::world::SpawnMode spawn_mode =
        state->mode == k_mode_world ? corundum::world::SpawnMode::World : corundum::world::SpawnMode::SingleMap;
    auto spawn = world::apply_spawn(engine, spawn_mode, state->map_or_world_id, state->active_zone, state->player_col,
                                    state->player_row);
    if (!spawn) {
      // apply_spawn may have replaced part of the render world before failing; drop the rest
      // rather than leave a half-built world behind.
      world::unload_world(engine);
      return std::unexpected(std::move(spawn).error());
    }

    engine.flags = std::move(state->flags);
    engine.entered_from_world = state->entered_from_world;
    return state->meta;
  }

} // namespace corundum::save
