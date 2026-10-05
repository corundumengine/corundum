// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/update.hpp>

#include "font_fixtures.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

/// Fixtures shared by the world-transition tests and the root inventory-toggle
/// test: both boot the same fixture world through a null platform. Every helper
/// is `inline` so the two translation units share one definition.
namespace corundum::test {

  inline core::GameConfig make_world_config(const std::filesystem::path &fixtures) {
    core::GameConfig cfg{};
    cfg.window_title = "world_transition_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures / "sprites").string();
    cfg.paths.font_dir = (fixtures / "fonts").string();
    set_missing_fonts(cfg.paths);
    cfg.paths.world_manifest_path = (fixtures / "worlds/transition/manifest.json").string();
    cfg.paths.spawn_points_dir = (fixtures / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

  inline void adopt_platform(Engine &engine, unsigned w, unsigned h) {
    platform::null::NullPlatform platform = platform::null::make_null_platform(w, h);
    platform::null::adopt_null_platform(engine, platform);
  }

  /// Run one fixed simulation step with a single action's `pressed` bit set. Used to drive
  /// the portal-confirm prompt's Select/Cancel path through update_transition_prompt().
  inline void advance_with(Engine &engine, input::Action action) {
    const auto map = world::build_map_view(engine.render, engine.cfg);
    input::InputState input{};
    input.pressed.set(static_cast<std::size_t>(action));
    world::update(engine.scene, engine.cfg, input, map, 1.f / 60.f, static_cast<float>(engine.window_width()),
                  static_cast<float>(engine.window_height()), engine.input_mapper.last_device());
  }

} // namespace corundum::test
