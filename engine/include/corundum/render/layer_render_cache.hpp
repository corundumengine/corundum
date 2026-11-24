// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdint>
#include <limits>
#include <vector>

namespace corundum::render {

/** Flat per-cell lookups the tile pass reads instead of a TilemapLayer's flat_maps.
 *
 *  Built from a loaded layer when its area loads, and owned beside that tilemap (MapData,
 *  ChunkEntry). Render state never mutates a tilemap after load, so the cache cannot go stale.
 *  Every vector is indexed by world::tilemap::cell_index().
 */
struct LayerRenderCache {
  /** animation_index value for a cell that is not animated. */
  static constexpr std::uint32_t k_no_animation = std::numeric_limits<std::uint32_t>::max();

  /** Cell → index into animations, or k_no_animation. */
  std::vector<std::uint32_t> animation_index{};

  /** Animated cells, densely packed. */
  std::vector<world::tilemap::AnimatedCell> animations{};

  /** Cell → k_flip_h | k_flip_v; 0 = no flip. */
  std::vector<std::uint8_t> flip_flags{};

  /** Highest elevation in the layer; 0 when the layer has no elevation data. */
  std::uint8_t max_elevation{0};
};

/** One cache per layer of `tilemap`, in layer order. */
[[nodiscard]] std::vector<LayerRenderCache> build_layer_render_caches(const world::tilemap::Tilemap &tilemap);

} // namespace corundum::render
