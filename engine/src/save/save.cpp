// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/save/save.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/engine.hpp>
#include <corundum/render/render_sys.hpp>
#include <corundum/world/transition.hpp>

#include <format>
#include <string>
#include <utility>

namespace corundum::save {

  namespace {

    constexpr std::string_view k_mode_world = "world";
    constexpr std::string_view k_mode_single_map = "single_map";

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

  std::expected<void, std::string> migrate(nlohmann::json & /*j*/, int from_version) noexcept {
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

    const int version = j.value("version", 1);
    if (version > k_save_version)
      return std::unexpected(
          std::format("save version {} is newer than supported version {}", version, k_save_version));

    nlohmann::json migrated = j;
    if (auto result = migrate(migrated, version); !result)
      return std::unexpected(std::move(result).error());

    SaveState s;
    s.version = k_save_version;
    s.game_id = migrated.value("game_id", std::string{});
    s.mode = migrated.value("mode", std::string{k_mode_single_map});
    s.map_or_world_id = migrated.value("map_or_world_id", std::string{});
    s.active_zone = migrated.value("active_zone", std::string{});
    s.player_col = migrated.value("player_col", 0.f);
    s.player_row = migrated.value("player_row", 0.f);
    s.entered_from_world = migrated.value("entered_from_world", false);

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
      state.map_or_world_id = engine.scene.zone_id;
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

    engine.flags = std::move(state->flags);
    engine.entered_from_world = state->entered_from_world;

    auto spawn = world::apply_spawn(engine, state->mode, state->map_or_world_id, state->active_zone, state->player_col,
                                    state->player_row);
    if (!spawn)
      return std::unexpected(std::move(spawn).error());
    return {};
  }

} // namespace corundum::save