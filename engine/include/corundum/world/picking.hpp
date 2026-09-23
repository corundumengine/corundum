// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/isometric.hpp>

#include <optional>

namespace corundum::world {

  class Camera;
  struct MapView; // defined in map_view.hpp — forward-declared to keep this header light.

  /**
   * @brief A tile-grid coordinate.
   *
   * Distinct from tools::tilesmith::TileCoord (Tilesmith is a separate codebase); this
   * is the engine-side equivalent for picking results.
   */
  struct TileCoord {
    int col{};

    int row{};

    [[nodiscard]] friend bool operator==(const TileCoord &, const TileCoord &) noexcept = default;
  };

  /**
   * @brief Convert a mouse position to the tile it's over, elevation-aware.
   *
   * @details Works in both single-map and chunked/world mode. In single-map mode elevation
   * spans the map's own tilemap (map.elevation_map); in world mode it uses
   * render::elevation_under via map.world_render, spanning the whole world grid. In world
   * mode a cell whose chunk is not resident reads as elevation 0, so a hover over an
   * unloaded region resolves to a tile rather than returning nullopt. Returns nullopt when
   * there is no elevation source (neither map.elevation_map nor map.world_render is set),
   * when camera.zoom is non-positive, or when the iso params are degenerate.
   *
   * For each candidate cell the mouse position is inverted assuming that cell's real
   * elevation (world_to_tile()) and accepted only if the result falls in that exact cell —
   * this makes the test diamond-exact with no separate point-in-polygon code. When a raised
   * tile's footprint visually overlaps a lower neighbor (both pass the test for the same
   * screen point), the candidate with the larger iso_depth_key() wins — the same "larger
   * key draws later/on top" convention the renderer uses, so picking agrees with what's
   * visually on top.
   *
   * @param mouse_x Window-pixel mouse X (InputState::mouse_x).
   * @param mouse_y Window-pixel mouse Y (InputState::mouse_y).
   * @param camera  Current camera (world-space top-left of the viewport); its zoom is applied
   *                as `world = screen / camera.zoom + camera.{x,y}`, matching the renderer.
   * @param map     Current map view.
   * @param iso     Packed projection params for the active map; iso.elev_step must already be
   *                scaled by tile_scale, as compute_isometric_params() produces it.
   * @return The topmost tile under the cursor, or nullopt if none (out of bounds,
   *         empty space, or no elevation source available).
   */
  [[nodiscard]] std::optional<TileCoord> pick_tile(float mouse_x, float mouse_y, const Camera &camera,
                                                   const MapView &map, const core::math::IsometricParams &iso) noexcept;

} // namespace corundum::world
