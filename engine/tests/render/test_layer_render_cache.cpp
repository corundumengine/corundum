// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/layer_render_cache.hpp>

#include <corundum/world/tilemap/tilemap.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tilemap = corundum::world::tilemap;
namespace render_data = corundum::render;

namespace {

  constexpr int k_width = 3;

  constexpr int k_height = 2;

  tilemap::TilemapLayer make_layer(std::string name) {
    tilemap::TilemapLayer layer;
    layer.name = std::move(name);
    return layer;
  }

  tilemap::Tilemap make_map(std::vector<tilemap::TilemapLayer> layers) {
    tilemap::Tilemap tm;
    tm.width = k_width;
    tm.height = k_height;
    tm.layers = std::move(layers);
    return tm;
  }

} // namespace

TEST_CASE("build_layer_render_caches: flip flags land on their cell index") {
  tilemap::TilemapLayer layer = make_layer("ground");
  layer.flip_flags = {{4, tilemap::k_flip_h}, {1, tilemap::k_flip_v}};

  const std::vector<render_data::LayerRenderCache> caches =
      render_data::build_layer_render_caches(make_map({std::move(layer)}));

  REQUIRE_EQ(caches.size(), 1u);
  CHECK_EQ(caches[0].flip_flags, (std::vector<std::uint8_t>{0, tilemap::k_flip_v, 0, 0, tilemap::k_flip_h, 0}));
}

TEST_CASE("build_layer_render_caches: animated cells are packed densely in cell order") {
  tilemap::TilemapLayer layer = make_layer("ground");
  tilemap::AnimatedCell torch;
  torch.anim_name = "torch";
  torch.frame_gids = {7, 8};
  tilemap::AnimatedCell still;
  still.anim_name = "still";
  still.frame_gids = {9};
  layer.animated_cells = {{5, torch}, {0, still}};

  const std::vector<render_data::LayerRenderCache> caches =
      render_data::build_layer_render_caches(make_map({std::move(layer)}));

  REQUIRE_EQ(caches.size(), 1u);
  CHECK_EQ(caches[0].animation_index, (std::vector<std::uint32_t>{0, render_data::LayerRenderCache::k_no_animation,
                                                                  render_data::LayerRenderCache::k_no_animation,
                                                                  render_data::LayerRenderCache::k_no_animation,
                                                                  render_data::LayerRenderCache::k_no_animation, 1}));
  REQUIRE_EQ(caches[0].animations.size(), 2u);
  CHECK_EQ(caches[0].animations[0].frame_gids, std::vector<tilemap::TileId>{9});
  CHECK_EQ(caches[0].animations[1].frame_gids, (std::vector<tilemap::TileId>{7, 8}));
}

TEST_CASE("build_layer_render_caches: max_elevation is the layer's highest elevation") {
  tilemap::TilemapLayer raised = make_layer("raised");
  raised.elevation = {0, 3, 1, 0, 2, 0};
  tilemap::TilemapLayer flat = make_layer("flat");

  const std::vector<render_data::LayerRenderCache> caches =
      render_data::build_layer_render_caches(make_map({std::move(raised), std::move(flat)}));

  REQUIRE_EQ(caches.size(), 2u);
  CHECK_EQ(caches[0].max_elevation, 3);
  CHECK_EQ(caches[1].max_elevation, 0);
}

TEST_CASE("build_layer_render_caches: one cache per layer, in layer order") {
  tilemap::TilemapLayer ground = make_layer("ground");
  ground.flip_flags = {{0, tilemap::k_flip_h}, {2, tilemap::k_flip_v}};
  tilemap::TilemapLayer roof = make_layer("roof");
  roof.flip_flags = {{5, tilemap::k_flip_h}};

  const std::vector<render_data::LayerRenderCache> caches =
      render_data::build_layer_render_caches(make_map({std::move(ground), std::move(roof)}));

  REQUIRE_EQ(caches.size(), 2u);
  CHECK_EQ(caches[0].flip_flags, (std::vector<std::uint8_t>{tilemap::k_flip_h, 0, tilemap::k_flip_v, 0, 0, 0}));
  CHECK_EQ(caches[1].flip_flags, (std::vector<std::uint8_t>{0, 0, 0, 0, 0, tilemap::k_flip_h}));
}
