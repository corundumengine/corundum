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
