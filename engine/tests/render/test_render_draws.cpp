// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/render/area.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <memory>

#include "memory_content_source.hpp"
#include "recording_renderer.hpp"
#include "render_state_fixtures.hpp"
#include "world_transition_fixtures.hpp"

using corundum::test::disk;

namespace {

  using corundum::render::RenderState;
  using corundum::test::TileRecordingRenderer;
  namespace tilemap = corundum::world::tilemap;

  /// Non-empty cells in every visible layer at or above the ground band.
  std::size_t count_drawable_cells(const tilemap::Tilemap &tm) {
    std::size_t count = 0;
    for (const tilemap::TilemapLayer &layer : tm.layers) {
      if (!layer.visible || layer.z_index < 0)
        continue;
      REQUIRE(layer.baked_animations.empty());
      for (int row = 0; row < tm.height; ++row)
        for (int col = 0; col < tm.width; ++col)
          if (tm.tile_at(layer, col, row) != tilemap::k_empty_tile)
            ++count;
    }
    return count;
  }

  void render_frame(TileRecordingRenderer &renderer, RenderState &state, const corundum::core::GameConfig &cfg,
                    const corundum::world::Scene &scene, int win_w, int win_h) {
    corundum::render::render(renderer, state, cfg, scene, 1.f, win_w, win_h);
  }

} // namespace

TEST_CASE("render: a single map draws every non-empty visible cell once") {
  const corundum::core::GameConfig cfg = corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
  TileRecordingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_map(
              disk(), renderer, state,
              (std::filesystem::path{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR} / "tilemaps/lifecycle_test.json").string(),
              cfg)
              .has_value());
  auto scene = std::make_unique<corundum::world::Scene>();
  scene->camera = {.x = -50000.f, .y = -50000.f, .zoom = 1.f};
  state.previous_step.camera = {.x = -50000.f, .y = -50000.f};
  state.previous_step.zoom = 1.f;

  render_frame(renderer, state, cfg, *scene, 100000, 100000);

  const std::size_t expected = count_drawable_cells(*corundum::render::active_tilemap(state));
  REQUIRE(expected > 0);
  CHECK_EQ(renderer.sprites.size(), expected);
}

TEST_CASE("render: a world draws every non-empty visible cell of every resident chunk once") {
  const corundum::core::GameConfig cfg = corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
  TileRecordingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_world(disk(), renderer, state, cfg, {}).has_value());
  auto scene = std::make_unique<corundum::world::Scene>();
  scene->camera = {.x = -50000.f, .y = -50000.f, .zoom = 1.f};
  state.previous_step.camera = {.x = -50000.f, .y = -50000.f};
  state.previous_step.zoom = 1.f;

  render_frame(renderer, state, cfg, *scene, 100000, 100000);

  const auto &world_area = corundum::test::world_area_of(state);
  std::size_t expected = 0;
  for (const corundum::render::ChunkEntry &chunk : world_area.chunks.active())
    expected += count_drawable_cells(chunk.tilemap);
  REQUIRE(expected > 0);
  CHECK_EQ(renderer.sprites.size(), expected);
}

TEST_CASE("render: a camera far from the area draws no tiles") {
  const corundum::core::GameConfig cfg = corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
  TileRecordingRenderer renderer;
  RenderState state;
  REQUIRE(corundum::render::load_map(
              disk(), renderer, state,
              (std::filesystem::path{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR} / "tilemaps/lifecycle_test.json").string(),
              cfg)
              .has_value());
  auto scene = std::make_unique<corundum::world::Scene>();
  scene->camera = {.x = 1e6f, .y = 1e6f, .zoom = 1.f};
  state.previous_step.camera = {.x = 1e6f, .y = 1e6f};
  state.previous_step.zoom = 1.f;

  render_frame(renderer, state, cfg, *scene, 320, 240);

  CHECK(renderer.sprites.empty());
}

TEST_CASE("render: a failed load_map after a single map draws nothing") {
  const std::filesystem::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  const corundum::core::GameConfig cfg = corundum::test::make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
  TileRecordingRenderer renderer;
  RenderState state;
  const std::filesystem::path lifecycle_map_path = fixtures / "tilemaps/lifecycle_test.json";
  const std::filesystem::path missing_map_path = fixtures / "tilemaps/missing.json";
  REQUIRE(corundum::render::load_map(disk(), renderer, state, lifecycle_map_path.string(), cfg).has_value());
  REQUIRE_FALSE(corundum::render::load_map(disk(), renderer, state, missing_map_path.string(), cfg).has_value());
  auto scene = std::make_unique<corundum::world::Scene>();
  scene->camera = {.x = -50000.f, .y = -50000.f, .zoom = 1.f};
  state.previous_step.camera = {.x = -50000.f, .y = -50000.f};
  state.previous_step.zoom = 1.f;

  render_frame(renderer, state, cfg, *scene, 100000, 100000);

  CHECK(renderer.sprites.empty());
}
