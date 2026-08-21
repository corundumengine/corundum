#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/render/sys/render_sys.hpp>

namespace tilemap = corundum::gameplay::world::tilemap;
namespace render_data = corundum::render::data;

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
  engine.render.active_chunks.push_back(std::move(chunk));

  CHECK(corundum::active_tilemap(engine) == nullptr);
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

  corundum::render::sys::snapshot_prev_frame(engine.render, engine.scene);

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
  namespace render_sys = corundum::render::sys;
  render_data::RenderState state;
  state.mode = render_data::RenderMode::World;
  state.manifest.chunk_size = 16;
  state.manifest.chunks_wide = 4;
  state.manifest.chunks_tall = 4;
  state.last_center_chunk = {0, 0};

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
  state.active_chunks.push_back(std::move(chunk00));

  state.chunk_slot_by_offset.fill(-1);
  state.chunk_slot_by_offset[static_cast<std::size_t>((0 + 1) * 3 + (0 + 1))] = 0; // (0, 0) → slot 0

  // col_f = -0.5: without fix, truncate → col=0, chunk (0, 0) cell (0, 0) returns 42.
  //                 with fix,    floor   → col=-1, chunk (-1, 0) absent → returns 0.
  CHECK(render_sys::elevation_under(state, -0.5f, 0.f) == doctest::Approx(0.f));
}
