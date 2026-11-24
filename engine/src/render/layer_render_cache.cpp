// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/layer_render_cache.hpp>

#include <corundum/world/tilemap/tilemap.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace corundum::render {

namespace {

LayerRenderCache build_layer_render_cache(const world::tilemap::TilemapLayer &layer, int width, int height) {
  const auto cell_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  LayerRenderCache cache;

  cache.flip_flags.assign(cell_count, std::uint8_t{0});
  for (const auto &[index, flags] : layer.flip_flags)
    if (index >= 0 && std::cmp_less(index, cell_count))
      cache.flip_flags[static_cast<std::size_t>(index)] = flags;

  cache.animation_index.assign(cell_count, LayerRenderCache::k_no_animation);
  cache.animations.reserve(layer.animated_cells.size());
  for (const auto &[index, animation] : layer.animated_cells) {
    if (index < 0 || std::cmp_greater_equal(index, cell_count))
      continue;
    cache.animation_index[static_cast<std::size_t>(index)] = static_cast<std::uint32_t>(cache.animations.size());
    cache.animations.push_back(animation);
  }

  if (!layer.elevation.empty())
    cache.max_elevation = std::ranges::max(layer.elevation);
  return cache;
}

} // namespace

std::vector<LayerRenderCache> build_layer_render_caches(const world::tilemap::Tilemap &tilemap) {
  std::vector<LayerRenderCache> caches;
  caches.reserve(tilemap.layers.size());
  for (const world::tilemap::TilemapLayer &layer : tilemap.layers)
    caches.push_back(build_layer_render_cache(layer, tilemap.width, tilemap.height));
  return caches;
}

} // namespace corundum::render
