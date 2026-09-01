#include <doctest/doctest.h>

#include "layer_presets.hpp"

#include <corundum/world/tilemap/tilemap.hpp>
#include <span>
#include <string>
#include <vector>

namespace tilemap = corundum::world::tilemap;
using tools::tilemap::dedup_layer_name;
using tools::tilemap::layer_name_taken;
using tools::tilemap::layer_preset_label;
using tools::tilemap::LayerPreset;
using tools::tilemap::make_layer_from_preset;

TEST_CASE("make_layer_from_preset — Ground produces name=z=0 depth=false") {
  const tilemap::TilemapLayer layer = make_layer_from_preset(LayerPreset::Ground, 10, 8, {});
  CHECK(layer.name == "Ground");
  CHECK(layer.z_index == 0);
  CHECK(layer.depth_sorted == false);
  CHECK(layer.visible == true);
  CHECK(layer.tiles.size() == 80);
}

TEST_CASE("make_layer_from_preset — Floor Detail and Water are also z_index=0") {
  const tilemap::TilemapLayer floor_detail = make_layer_from_preset(LayerPreset::FloorDetail, 4, 4, {});
  CHECK(floor_detail.name == "Floor Detail");
  CHECK(floor_detail.z_index == 0);
  CHECK(floor_detail.depth_sorted == false);

  const tilemap::TilemapLayer water = make_layer_from_preset(LayerPreset::Water, 4, 4, {});
  CHECK(water.name == "Water");
  CHECK(water.z_index == 0);
  CHECK(water.depth_sorted == false);
}

TEST_CASE("make_layer_from_preset — Walls is the only depth_sorted preset") {
  const tilemap::TilemapLayer walls = make_layer_from_preset(LayerPreset::Walls, 4, 4, {});
  CHECK(walls.name == "Walls");
  CHECK(walls.z_index == 1);
  CHECK(walls.depth_sorted == true);
}

TEST_CASE("make_layer_from_preset — Roof and Decor are z>0 but not depth_sorted") {
  const tilemap::TilemapLayer roof = make_layer_from_preset(LayerPreset::Roof, 4, 4, {});
  CHECK(roof.name == "Roof");
  CHECK(roof.z_index == 1);
  CHECK(roof.depth_sorted == false);

  const tilemap::TilemapLayer decor = make_layer_from_preset(LayerPreset::Decor, 4, 4, {});
  CHECK(decor.name == "Decor");
  CHECK(decor.z_index == 2);
  CHECK(decor.depth_sorted == false);
}

TEST_CASE("make_layer_from_preset — Blank reproduces old 'Layer {N}' naming") {
  const tilemap::TilemapLayer layer = make_layer_from_preset(LayerPreset::Blank, 4, 4, {});
  CHECK(layer.name == "Layer 1");
  CHECK(layer.z_index == 0);
  CHECK(layer.depth_sorted == false);

  // With existing layers present, N is existing_layers.size() + 1, NOT deduped.
  tilemap::TilemapLayer existing;
  existing.name = "Ground";
  existing.z_index = 0;
  const std::vector<tilemap::TilemapLayer> layers{existing};
  const tilemap::TilemapLayer layer2 = make_layer_from_preset(LayerPreset::Blank, 4, 4, layers);
  CHECK(layer2.name == "Layer 2");

  // "Layer 2" existing should produce "Layer 3" — not deduped.
  tilemap::TilemapLayer layer2_existing;
  layer2_existing.name = "Layer 2";
  layer2_existing.z_index = 0;
  const std::vector<tilemap::TilemapLayer> layers2{layer2_existing};
  const tilemap::TilemapLayer layer3 = make_layer_from_preset(LayerPreset::Blank, 4, 4, layers2);
  CHECK(layer3.name == "Layer 2");
}

TEST_CASE("make_layer_from_preset — tiles are filled with k_empty_tile") {
  const tilemap::TilemapLayer layer = make_layer_from_preset(LayerPreset::Walls, 5, 3, {});
  REQUIRE(layer.tiles.size() == 15);
  for (const auto &tile : layer.tiles)
    CHECK(tile == tilemap::k_empty_tile);
}

TEST_CASE("make_layer_from_preset — adding same preset twice dedupes by name") {
  tilemap::TilemapLayer existing_walls;
  existing_walls.name = "Walls";
  existing_walls.z_index = 1;
  existing_walls.depth_sorted = true;
  const std::vector<tilemap::TilemapLayer> layers{existing_walls};

  const tilemap::TilemapLayer second_walls = make_layer_from_preset(LayerPreset::Walls, 4, 4, layers);
  CHECK(second_walls.name == "Walls 2");
  CHECK(second_walls.z_index == 1);
  CHECK(second_walls.depth_sorted == true);
}

TEST_CASE("dedup_layer_name — same-z_index=0 presets dedupe independently by name") {
  // 'Ground' and 'Floor Detail' both produce z_index=0 layers but their names
  // are distinct, so adding one doesn't cause the other to dedupe.
  tilemap::TilemapLayer ground;
  ground.name = "Ground";
  ground.z_index = 0;
  const std::vector<tilemap::TilemapLayer> layers{ground};

  // A new Ground with Ground already present → "Ground 2".
  const tilemap::TilemapLayer second_ground = make_layer_from_preset(LayerPreset::Ground, 4, 4, layers);
  CHECK(second_ground.name == "Ground 2");

  // A new Floor Detail with only Ground present → still "Floor Detail" (no dedup needed).
  const tilemap::TilemapLayer floor_detail = make_layer_from_preset(LayerPreset::FloorDetail, 4, 4, layers);
  CHECK(floor_detail.name == "Floor Detail");
}

TEST_CASE("dedup_layer_name — finds the first free suffix") {
  tilemap::TilemapLayer l1;
  l1.name = "Walls";
  tilemap::TilemapLayer l2;
  l2.name = "Walls 2";
  tilemap::TilemapLayer l3;
  l3.name = "Walls 3";
  const std::vector<tilemap::TilemapLayer> layers{l1, l2, l3};

  CHECK(dedup_layer_name("Walls", layers) == "Walls 4");
}

TEST_CASE("dedup_layer_name — base name free, returns base unchanged") {
  CHECK(dedup_layer_name("Roof", {}) == "Roof");
}

TEST_CASE("layer_name_taken — matches by exact string equality") {
  tilemap::TilemapLayer l;
  l.name = "Ground";
  const std::vector<tilemap::TilemapLayer> layers{l};

  CHECK(layer_name_taken("Ground", layers));
  CHECK_FALSE(layer_name_taken("Ground 2", layers));
  CHECK_FALSE(layer_name_taken("ground", layers));
}

TEST_CASE("layer_preset_label — returns the canonical name for each preset") {
  CHECK(layer_preset_label(LayerPreset::Ground) == "Ground");
  CHECK(layer_preset_label(LayerPreset::FloorDetail) == "Floor Detail");
  CHECK(layer_preset_label(LayerPreset::Water) == "Water");
  CHECK(layer_preset_label(LayerPreset::Walls) == "Walls");
  CHECK(layer_preset_label(LayerPreset::Roof) == "Roof");
  CHECK(layer_preset_label(LayerPreset::Decor) == "Decor");
  CHECK(layer_preset_label(LayerPreset::Blank) == "Blank");
}
