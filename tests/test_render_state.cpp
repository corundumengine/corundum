#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_sys.hpp>

namespace tilemap = corundum::gameplay::world::tilemap;
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
    layer.tiles.assign(static_cast<std::size_t>(tm.width * tm.height), 1);
    tm.layers.push_back(std::move(layer));
    return tm;
  }

} // namespace

TEST_CASE("active_tilemap: None mode returns nullptr") {
  corundum::Engine engine;
  engine.render.mode = render_data::RenderMode::None;

  CHECK(corundum::active_tilemap(engine) == nullptr);
  CHECK(render_data::active_tilemap(engine.render) == nullptr);
}

TEST_CASE("active_tilemap: SingleMap mode returns &map_data.tilemap") {
  corundum::Engine engine;
  engine.render.mode = render_data::RenderMode::SingleMap;
  engine.render.map_data.tilemap = make_flat_map();

  const auto *tm = corundum::active_tilemap(engine);
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

  CHECK(corundum::active_tilemap(engine) == nullptr);
}

TEST_CASE("NullRenderer: stats() returns zero draw/quad/dropped counts and begin_frame reports success") {
  corundum::platform::null::NullRenderer renderer;
  CHECK(renderer.stats().draw_calls == 0);
  CHECK(renderer.stats().quads == 0);
  CHECK(renderer.stats().dropped_quads == 0);
  CHECK(renderer.begin_frame({}));

  static_assert(std::is_same_v<decltype(renderer.stats()), corundum::platform::RendererStats>);
}

TEST_CASE("snapshot_prev_frame: copies live transforms and camera into prev_* fields") {
  corundum::Engine engine;
  corundum::gameplay::component::TransformTable &transforms = engine.scene.world.transforms;
  transforms.col[0] = 3.5f;
  transforms.row[0] = 7.25f;
  transforms.col[1] = 10.f;
  transforms.row[1] = 2.f;
  transforms.count = 2;
  engine.scene.camera.x = 100.f;
  engine.scene.camera.y = 50.f;
  engine.scene.camera.zoom = 2.f;

  corundum::render::snapshot_prev_frame(engine.render, engine.scene);

  CHECK(engine.render.prev_count == 2);
  CHECK(engine.render.prev_col[0] == 3.5f);
  CHECK(engine.render.prev_row[0] == 7.25f);
  CHECK(engine.render.prev_col[1] == 10.f);
  CHECK(engine.render.prev_row[1] == 2.f);
  CHECK(engine.render.prev_cam_x == 100.f);
  CHECK(engine.render.prev_cam_y == 50.f);
  CHECK(engine.render.prev_zoom == 2.f);
}

TEST_CASE("elevation_under — negative col_f returns 0 (no chunk at floor cell), not the truncated cell's elevation") {
  // Latent bug: elevation_under used `static_cast<int>(col_f)` which truncates toward
  // zero, so col_f = -0.5 became col = 0 and the lookup went to chunk (0, 0) cell (0, ...).
  // Floor gives col = -1, identifying the true cell as belonging to chunk (-1, 0) — which
  // isn't in the active window — so the lookup correctly returns 0. Today positions are
  // clamped >= 0 so this never fires; standardizing on std::floor closes the seam so it
  // can't bite when anything (camera shake, knockback, chunk-local coords) goes negative.
  namespace render_sys = corundum::render;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 4;
  state.manifest.chunks_tall = 4;
  state.chunks.set_last_center({0, 0});

  // Single active chunk (0, 0) with cell (0, 0) elevation = 42 — distinguishable from 0.
  render_data::ChunkEntry chunk00;
  chunk00.coord = {0, 0};
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

  state.chunks.rebuild_slot_table(); // (0, 0) → slot 0

  // col_f = -0.5: without fix, truncate → col=0, chunk (0, 0) cell (0, 0) returns 42.
  //                 with fix,    floor   → col=-1, chunk (-1, 0) absent → returns 0.
  CHECK(render_sys::elevation_under(state, -0.5f, 0.f) == doctest::Approx(0.f));
}

TEST_CASE("load_one_pending_chunk: a freshly loaded chunk marks chunks_dirty") {
  namespace render_sys = corundum::render;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 1;
  state.manifest.chunks_tall = 1;
  state.manifest.base_dir = std::filesystem::path(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR) / "tilemaps";
  state.chunks.enqueue_pending({0, 0}); // resolves to base_dir/chunk_0_0.json
  state.chunks.clear_dirty();           // simulate "already synced this frame" before the load

  corundum::platform::null::NullRenderer renderer;
  corundum::core::GameConfig cfg{};

  const bool loaded = render_sys::load_one_pending_chunk(renderer, state, cfg);

  REQUIRE(loaded);
  CHECK(state.chunks.active_size() == 1);
  CHECK(state.chunks.dirty()); // fails before the fix
}

TEST_CASE("chunk window shift: crossing a boundary in a 7x7 world streams new chunks in and prunes far ones") {
  // Mirrors sync_active_chunks(): the fixed 3x3 window (radius 1). In a world LARGER than
  // the window, moving the player to a new center chunk must enqueue the newly-adjacent
  // column and prune the one that fell out of range — i.e. actual chunk streaming.
  //
  // Village is 7x7 (chunk_0_0..chunk_6_6). Start centered at (3,3): active = cols 2..4 x rows 2..4.
  // Player walks east one chunk: new center (4,3) -> desired cols 3..5 x rows 2..4.
  // Expect col 5 row 2..4 to be enqueued (stream in) and col 2 row 2..4 to be pruned (stream out).
  constexpr int k_chunk_size = 16;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = k_chunk_size;
  state.manifest.chunks_wide = 7;
  state.manifest.chunks_tall = 7;

  const tilemap::ChunkCoord start_center{3, 3};
  state.chunks.set_last_center(start_center);
  for (int dx = -1; dx <= 1; ++dx) {
    for (int dy = -1; dy <= 1; ++dy) {
      render_data::ChunkEntry e;
      e.coord = {start_center.col + dx, start_center.row + dy};
      e.tilemap = make_flat_map();
      state.chunks.add_active(std::move(e));
    }
  }
  state.chunks.rebuild_slot_table();

  // Player moved one chunk east: recompute the desired 3x3 window as sync_active_chunks does.
  const tilemap::ChunkCoord new_center{4, 3};
  state.chunks.set_last_center(new_center);
  std::array<tilemap::ChunkCoord, 9> desired{};
  int desired_count = 0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      desired[desired_count++] = {new_center.col + dx, new_center.row + dy};
  const std::span<const tilemap::ChunkCoord> desired_span{desired.data(), static_cast<std::size_t>(desired_count)};
  const auto in_desired = [&](const render_data::ChunkEntry &e) {
    return std::ranges::find(desired_span, e.coord) != desired_span.end();
  };

  state.chunks.prune_active(in_desired);
  state.chunks.clear_dirty();
  for (const tilemap::ChunkCoord c : desired_span)
    if (!state.chunks.has(c))
      state.chunks.enqueue_pending(c);
  state.chunks.rebuild_slot_table();

  // The west column (col 2) fell outside the window and was pruned.
  CHECK_FALSE(state.chunks.has({2, 2}));
  CHECK_FALSE(state.chunks.has({2, 3}));
  CHECK_FALSE(state.chunks.has({2, 4}));

  // The east column (col 5) is now requested -> queued for loading (streams in on the next
  // load_one_pending_chunk). Drain the pending queue the way the loader does and collect it.
  std::vector<tilemap::ChunkCoord> pending_coords;
  tilemap::ChunkCoord c;
  while (state.chunks.pop_pending(c))
    pending_coords.push_back(c);
  CHECK(pending_coords.size() == 3); // {5,2}, {5,3}, {5,4} — the newly-adjacent east column
  const bool enqueued =
      std::ranges::all_of(std::array<tilemap::ChunkCoord, 3>{{{5, 2}, {5, 3}, {5, 4}}},
                          [&](tilemap::ChunkCoord e) { return std::ranges::contains(pending_coords, e); });
  CHECK(enqueued);

  // The surviving loaded window is cols 3..4 x rows 2..4 (the cols common to both the old
  // and new center windows; the new col 5 is only queued, ready to load next frame).
  CHECK(state.chunks.has({3, 2}));
  CHECK(state.chunks.has({4, 4}));
  // A 7x7 world means the player can keep walking further out; the window shifted, not fixed.
  CHECK(state.chunks.last_center() == new_center);
}
