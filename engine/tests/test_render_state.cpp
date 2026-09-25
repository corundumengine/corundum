// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <corundum/core/game_config.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/render/render_state.hpp>
#include <cstddef>
#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_system.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <type_traits>
#include <utility>
#include <vector>

namespace tilemap = corundum::world::tilemap;
namespace render_data = corundum::render;

namespace {

  tilemap::Tilemap make_flat_map() {
    tilemap::Tilemap tm;
    tm.width = 2;
    tm.height = 2;
    tilemap::TilemapLayer layer;
    layer.name = "ground";
    layer.z_index = 0;
    layer.visible = true;
    layer.tiles.assign(static_cast<std::size_t>(tm.width) * static_cast<std::size_t>(tm.height), 1);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

  // A 7x7 world whose chunk window starts centred at (3,3), then shifts one chunk east to
  // (4,3) — mirroring sync_active_chunks()'s recenter → prune → enqueue sequence. Returns the
  // state with the new centre applied and the newly-adjacent east column queued for loading.
  render_data::RenderState shifted_window_state() {
    constexpr int k_chunk_size = 16;
    render_data::RenderState state;
    state.mode = render_data::RenderMode::World;
    state.manifest.chunk_size = k_chunk_size;
    state.manifest.chunks_wide = 7;
    state.manifest.chunks_tall = 7;

    const tilemap::ChunkCoord start_center{.col = 3, .row = 3};
    state.chunks.set_last_center(start_center);
    for (const tilemap::ChunkCoord coord :
         tilemap::active_chunk_coords(start_center, render_data::ChunkWindow::k_radius, state.manifest)) {
      render_data::ChunkEntry e;
      e.coord = coord;
      e.tilemap = make_flat_map();
      state.chunks.add_active(std::move(e));
    }

    const tilemap::ChunkCoord new_center{.col = 4, .row = 3};
    state.chunks.set_last_center(new_center);
    const std::vector<tilemap::ChunkCoord> desired =
        tilemap::active_chunk_coords(new_center, render_data::ChunkWindow::k_radius, state.manifest);
    state.chunks.prune_active(
        [&](const render_data::ChunkEntry &e) { return std::ranges::contains(desired, e.coord); });
    (void)state.chunks.consume_dirty();
    for (const tilemap::ChunkCoord c : desired)
      if (!state.chunks.has(c))
        state.chunks.enqueue_pending(c);
    return state;
  }

} // namespace

TEST_CASE("active_tilemap: None mode returns nullptr") {
  corundum::Engine engine;
  engine.render.mode = render_data::RenderMode::None;

  CHECK(engine.active_tilemap() == nullptr);
  CHECK(render_data::active_tilemap(engine.render) == nullptr);
}

TEST_CASE("active_tilemap: SingleMap mode returns &map_data.tilemap") {
  corundum::Engine engine;
  engine.render.mode = render_data::RenderMode::SingleMap;
  engine.render.map_data.tilemap = make_flat_map();

  const auto *tm = engine.active_tilemap();
  REQUIRE(tm != nullptr);
  CHECK(tm == &engine.render.map_data.tilemap);
  CHECK(tm->width == 2);
  CHECK(tm->height == 2);
}

TEST_CASE("active_tilemap: World mode returns nullptr even with chunks loaded") {
  corundum::Engine engine;
  engine.render.mode = render_data::RenderMode::World;

  render_data::ChunkEntry chunk;
  chunk.tilemap = make_flat_map();
  engine.render.chunks.add_active(std::move(chunk));

  CHECK(engine.active_tilemap() == nullptr);
}

TEST_CASE("NullRenderer: stats() returns zero draw/quad/dropped counts and begin_frame reports success") {
  corundum::platform::null::NullRenderer renderer;
  CHECK(renderer.stats().draw_calls == 0);
  CHECK(renderer.stats().quads == 0);
  CHECK(renderer.stats().dropped_quads == 0);
  CHECK(renderer.begin_frame({}));

  static_assert(std::is_same_v<decltype(renderer.stats()), corundum::platform::RendererStats>);
}

TEST_CASE("snapshot_previous_step: keys entity positions by entity index and copies the camera") {
  corundum::Engine engine;
  corundum::entities::TransformTable &transforms = engine.scene.world.transforms;
  const corundum::entities::EntityId a{.index = 5, .generation = 1};
  const corundum::entities::EntityId b{.index = 2, .generation = 3};
  transforms.insert(a, 3.5f, 7.25f, 0.f, 0.f);
  transforms.insert(b, 10.f, 2.f, 0.f, 0.f);
  engine.scene.camera.x = 100.f;
  engine.scene.camera.y = 50.f;
  engine.scene.camera.zoom = 2.f;

  corundum::render::snapshot_previous_step(engine.render, engine.scene);

  CHECK(engine.render.prev_col[5] == 3.5f);
  CHECK(engine.render.prev_row[5] == 7.25f);
  CHECK(engine.render.prev_generation[5] == 1u);
  CHECK(engine.render.prev_col[2] == 10.f);
  CHECK(engine.render.prev_row[2] == 2.f);
  CHECK(engine.render.prev_generation[2] == 3u);
  CHECK(engine.render.prev_cam_x == 100.f);
  CHECK(engine.render.prev_cam_y == 50.f);
  CHECK(engine.render.prev_zoom == 2.f);
}

TEST_CASE("interpolated_tile_position: a swap-and-pop removal after the snapshot does not misattribute it") {
  corundum::render::RenderState state;
  corundum::world::Scene scene;
  const corundum::entities::EntityId first{.index = 0, .generation = 1};
  const corundum::entities::EntityId last{.index = 1, .generation = 1};
  scene.world.transforms.insert(first, 0.f, 0.f, 0.f, 0.f);
  scene.world.transforms.insert(last, 10.f, 10.f, 0.f, 0.f);
  corundum::render::snapshot_previous_step(state, scene);

  scene.world.transforms.remove(first); // swap-and-pop moves `last` into dense slot 0

  const auto drawn = corundum::render::interpolated_tile_position(state, last, 12.f, 14.f, 0.5f);
  CHECK(drawn.x == doctest::Approx(11.f));
  CHECK(drawn.y == doctest::Approx(12.f));
}

TEST_CASE("interpolated_tile_position: no snapshot, or a recycled index, draws the current position") {
  corundum::render::RenderState state;
  const corundum::entities::EntityId fresh{.index = 3, .generation = 1};
  const auto unsnapshotted = corundum::render::interpolated_tile_position(state, fresh, 4.f, 6.f, 0.5f);
  CHECK(unsnapshotted.x == doctest::Approx(4.f));
  CHECK(unsnapshotted.y == doctest::Approx(6.f));

  state.prev_col[3] = 0.f;
  state.prev_row[3] = 0.f;
  state.prev_generation[3] = 1;
  const corundum::entities::EntityId recycled{.index = 3, .generation = 2};
  const auto drawn = corundum::render::interpolated_tile_position(state, recycled, 4.f, 6.f, 0.5f);
  CHECK(drawn.x == doctest::Approx(4.f));
  CHECK(drawn.y == doctest::Approx(6.f));
}

TEST_CASE("render: alpha 0 draws the start-of-step camera and alpha 1 the current camera") {
  namespace render_system = corundum::render;

  corundum::Engine engine;
  corundum::platform::null::NullRenderer renderer;
  engine.render.prev_cam_x = 100.f;
  engine.render.prev_cam_y = 50.f;
  engine.render.prev_zoom = 2.f;
  engine.scene.camera.x = 140.f;
  engine.scene.camera.y = 80.f;
  engine.scene.camera.zoom = 1.5f;

  render_system::render(renderer, engine.render, engine.cfg, engine.scene, engine.flags, nullptr, 0.f, 800, 600);
  CHECK(renderer.last_camera_top_left().x == doctest::Approx(100.f));
  CHECK(renderer.last_camera_top_left().y == doctest::Approx(50.f));
  CHECK(renderer.last_zoom() == doctest::Approx(2.f));

  render_system::render(renderer, engine.render, engine.cfg, engine.scene, engine.flags, nullptr, 1.f, 800, 600);
  CHECK(renderer.last_camera_top_left().x == doctest::Approx(140.f));
  CHECK(renderer.last_camera_top_left().y == doctest::Approx(80.f));
  CHECK(renderer.last_zoom() == doctest::Approx(1.5f));
}

TEST_CASE("render: a normal single-step frame still blends the camera between snapshot and current") {
  namespace render_system = corundum::render;

  corundum::Engine engine;
  corundum::platform::null::NullRenderer renderer;

  engine.render.prev_cam_x = 100.f;
  engine.render.prev_cam_y = 50.f;
  engine.render.prev_zoom = 2.f;
  engine.scene.camera.x = 140.f;
  engine.scene.camera.y = 80.f;
  engine.scene.camera.zoom = 1.5f;

  render_system::render(renderer, engine.render, engine.cfg, engine.scene, engine.flags, nullptr, 0.5f, 800, 600);

  CHECK(renderer.last_camera_top_left().x == doctest::Approx(120.f));
  CHECK(renderer.last_camera_top_left().y == doctest::Approx(65.f));
  CHECK(renderer.last_zoom() == doctest::Approx(1.75f));
}

TEST_CASE("elevation_under — negative col_f returns 0 (no chunk at floor cell), not the truncated cell's elevation") {
  // Latent bug: elevation_under used `static_cast<int>(col_f)` which truncates toward
  // zero, so col_f = -0.5 became col = 0 and the lookup went to chunk (0, 0) cell (0, ...).
  // Floor gives col = -1, identifying the true cell as belonging to chunk (-1, 0) — which
  // isn't in the active window — so the lookup correctly returns 0. Today positions are
  // clamped >= 0 so this never fires; standardizing on std::floor closes the seam so it
  // can't bite when anything (camera shake, knockback, chunk-local coords) goes negative.
  namespace render_system = corundum::render;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 4;
  state.manifest.chunks_tall = 4;
  state.chunks.set_last_center({.col = 0, .row = 0});

  // Single active chunk (0, 0) with cell (0, 0) elevation = 42 — distinguishable from 0.
  render_data::ChunkEntry chunk00;
  chunk00.coord = {.col = 0, .row = 0};
  chunk00.tilemap.width = 16;
  chunk00.tilemap.height = 16;
  tilemap::TilemapLayer layer;
  layer.name = "ground";
  layer.z_index = 0;
  layer.visible = true;
  layer.tiles.assign(static_cast<std::size_t>(16 * 16), 1); // non-empty at every cell
  layer.elevation.assign(static_cast<std::size_t>(16 * 16), 0);
  layer.elevation[0] = 42; // (col=0, row=0)
  chunk00.tilemap.layers.push_back(std::move(layer));
  state.chunks.add_active(std::move(chunk00));

  // col_f = -0.5: without fix, truncate → col=0, chunk (0, 0) cell (0, 0) returns 42.
  //                 with fix,    floor   → col=-1, chunk (-1, 0) absent → returns 0.
  CHECK(render_system::elevation_under(state, -0.5f, 0.f) == doctest::Approx(0.f));
}

TEST_CASE("load_one_pending_chunk: a freshly loaded chunk marks the chunk window dirty") {
  namespace render_system = corundum::render;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 1;
  state.manifest.chunks_tall = 1;
  state.manifest.base_dir = std::filesystem::path(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR) / "tilemaps";
  state.chunks.enqueue_pending({.col = 0, .row = 0}); // resolves to base_dir/chunk_0_0.json
  (void)state.chunks.consume_dirty();                 // simulate "already synced this frame" before the load

  corundum::platform::null::NullRenderer renderer;
  const corundum::core::GameConfig cfg{};

  const bool loaded = render_system::load_one_pending_chunk(renderer, state, cfg);

  REQUIRE(loaded);
  CHECK(state.chunks.active_size() == 1);
  CHECK(state.chunks.consume_dirty()); // fails before the fix
}

TEST_CASE("chunk window shift: moving the window prunes the column that fell outside it") {
  // Mirrors sync_active_chunks(): the fixed 3x3 window (radius 1). In a world LARGER than
  // the window, moving the player to a new center chunk must prune the column that fell out
  // of range and keep the columns common to both windows — i.e. actual chunk streaming.
  //
  // Village is 7x7 (chunk_0_0..chunk_6_6). Start centered at (3,3): active = cols 2..4 x rows 2..4.
  // Player walks east one chunk: new center (4,3) -> desired cols 3..5 x rows 2..4.
  render_data::RenderState state = shifted_window_state();

  const std::vector<tilemap::ChunkCoord> k_pruned_column{
      {.col = 2, .row = 2},
      {.col = 2, .row = 3},
      {.col = 2, .row = 4},
  };
  const std::vector<tilemap::ChunkCoord> k_surviving_column{
      {.col = 3, .row = 2},
      {.col = 4, .row = 4},
  };
  CHECK(std::ranges::none_of(k_pruned_column, [&](tilemap::ChunkCoord c) { return state.chunks.has(c); }));
  CHECK(std::ranges::all_of(k_surviving_column, [&](tilemap::ChunkCoord c) { return state.chunks.has(c); }));
}

TEST_CASE("chunk window shift: the newly-adjacent column is queued and the centre advances") {
  // Continuation of the shift above: the east column (col 5) is the only new chunk the window
  // needs, so it is queued for load_one_pending_chunk to stream in on a later frame.
  render_data::RenderState state = shifted_window_state();

  std::vector<tilemap::ChunkCoord> pending_coords;
  tilemap::ChunkCoord c;
  while (state.chunks.pop_pending(c))
    pending_coords.push_back(c);

  const std::vector<tilemap::ChunkCoord> k_east_column{
      {.col = 5, .row = 2},
      {.col = 5, .row = 3},
      {.col = 5, .row = 4},
  };
  CHECK(std::ranges::is_permutation(k_east_column, pending_coords));
  // A 7x7 world means the player can keep walking further out; the window shifted, not fixed.
  CHECK(state.chunks.last_center() == tilemap::ChunkCoord{.col = 4, .row = 3});
}

TEST_CASE("chunk window: a chunk streamed out and back in keeps its back-to-front draw order") {
  // Regression: the render pass draws chunks in active() order, so a re-streamed chunk must
  // land where the boot window had it, not at the end. Appending it left it drawing on top of
  // the chunk in front of it after a walk away and back — their overlapping art at the shared
  // seam flipped, making the boundary look like one tilemap riding slightly over the other.
  constexpr int k_chunk_size = 16;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = k_chunk_size;
  state.manifest.chunks_wide = 7;
  state.manifest.chunks_tall = 7;

  const auto coords_of = [](const render_data::ChunkWindow &window) {
    std::vector<tilemap::ChunkCoord> coords;
    for (const render_data::ChunkEntry &e : window.active())
      coords.push_back(e.coord);
    return coords;
  };

  // Mirrors sync_active_chunks() + load_one_pending_chunk(): recenter, prune what fell outside
  // the radius-1 window, then add each newly-adjacent chunk as it finishes loading.
  const auto load_window = [&](tilemap::ChunkCoord center) {
    state.chunks.set_last_center(center);
    const std::vector<tilemap::ChunkCoord> desired =
        tilemap::active_chunk_coords(center, render_data::ChunkWindow::k_radius, state.manifest);
    state.chunks.prune_active(
        [&](const render_data::ChunkEntry &e) { return std::ranges::contains(desired, e.coord); });
    for (const tilemap::ChunkCoord c : desired) {
      if (state.chunks.has(c))
        continue;
      render_data::ChunkEntry e;
      e.coord = c;
      e.tilemap = make_flat_map();
      state.chunks.add_active(std::move(e));
    }
  };

  load_window({.col = 3, .row = 3});
  const std::vector<tilemap::ChunkCoord> boot_order = coords_of(state.chunks);

  // Walk south one chunk, then back: the row that streams out and back in must not reorder.
  load_window({.col = 3, .row = 4});
  load_window({.col = 3, .row = 3});

  CHECK(coords_of(state.chunks) == boot_order);
  // The invariant behind it: back-to-front == ascending (row, col).
  CHECK(std::ranges::is_sorted(state.chunks.active(),
                               [](const render_data::ChunkEntry &a, const render_data::ChunkEntry &b) {
                                 return std::pair{a.coord.row, a.coord.col} < std::pair{b.coord.row, b.coord.col};
                               }));
}
