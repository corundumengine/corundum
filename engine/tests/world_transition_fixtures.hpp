// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "null_platform.hpp"
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/update.hpp>

#include "font_fixtures.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

// Fixtures shared by the tests that boot the fixture world through a null platform and drive
// Engine frames. Every helper is `inline` so the translation units share one definition.
namespace corundum::test {

/** The steady-clock reading every test frame passes to Engine::run_frame. Because it never changes,
 *  a frame adds no wall time, so a test's fixed steps are exactly what it puts in timer.accumulator.
 */
constexpr std::chrono::steady_clock::time_point k_frame_time{};

/** Run one frame owing the timer exactly one fixed step, so a case advances the simulation once per
 *  call. The frame's reading is k_frame_time, so tick() itself adds no time.
 *  @return true while the loop should continue; false once quit is requested.
 */
[[nodiscard]] inline bool run_one_step(Engine &engine) {
  engine.timer.accumulator = engine.timer.target_dt;
  return engine.run_frame(k_frame_time);
}

inline core::GameConfig make_world_config(const std::filesystem::path &fixtures) {
  core::GameConfig config{};
  config.window_title = "world_transition_test";
  config.win_w = 320;
  config.win_h = 240;
  config.show_title = false;
  config.paths.sprites_dir = (fixtures / "sprites").string();
  config.paths.font_dir = (fixtures / "fonts").string();
  set_missing_fonts(config.paths);
  config.paths.world_manifest_path = (fixtures / "worlds/transition/manifest.json").string();
  config.paths.spawn_points_dir = (fixtures / "spawn_points").string();
  config.paths.portals_dir = (fixtures / "portals").string();
  config.paths.dialogue_dir.clear();
  config.paths.quests_dir.clear();
  config.paths.sounds_dir.clear();
  return config;
}

inline void adopt_platform(Engine &engine, unsigned w, unsigned h) {
  platform::null::NullPlatform platform = platform::null::make_null_platform(w, h);
  platform::null::adopt_null_platform(engine, platform);
}

/** Initialise `engine` and build the configured initial scene, as a game that skips the Title
 *  does. Engine::initialize() itself loads no world.
 */
[[nodiscard]] inline std::expected<void, std::string> initialize_with_world(Engine &engine, core::GameConfig config) {
  if (std::expected<void, std::string> result = engine.initialize(std::move(config)); !result)
    return result;
  return world::load_initial_scene(engine);
}

/** Run one fixed simulation step with a single action's `pressed` bit set. Used to drive
 *  the portal-confirm prompt's Select/Cancel path through update_transition_prompt().
 */
inline void advance_with(Engine &engine, input::Action action) {
  const auto map = world::build_map_view(engine.render, engine.config);
  input::InputState input{};
  input.pressed.set(static_cast<std::size_t>(action));
  world::update(engine.scene, engine.config, input, map, 1.f / 60.f, static_cast<float>(engine.window_width()),
                static_cast<float>(engine.window_height()), engine.input_mapper.last_device());
}

} // namespace corundum::test
