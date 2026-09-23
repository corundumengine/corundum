// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/isometric.hpp>
#include <corundum/world/world_bounds.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace corundum::world::tilemap {

  /// Grid coordinate identifying a single chunk in the world map (column, row).
  struct ChunkCoord {
    int col{}; ///< Column (west→east).
    int row{}; ///< Row (north→south).

    [[nodiscard]] bool operator==(const ChunkCoord &) const noexcept = default;
  };

  /// Parsed world manifest — describes the chunk grid layout.
  struct WorldManifest {
    int chunk_size{};  ///< Tiles per chunk side (commonly 128).
    int chunks_wide{}; ///< Number of chunks along X (commonly 16).
    int chunks_tall{}; ///< Number of chunks along Y (commonly 8).

    /// Effective tile columns / rows that actually contain non-empty tiles.
    /// When both are 0 the game falls back to chunks_wide × chunk_size
    /// (i.e. the full grid — no trimming needed).
    int tiles_wide{};
    int tiles_tall{};

    /// Effective tile columns: @ref tiles_wide when positive, else the full chunk grid.
    [[nodiscard]] int effective_tiles_wide() const noexcept {
      return tiles_wide > 0 ? tiles_wide : chunks_wide * chunk_size;
    }

    /// Effective tile rows: @ref tiles_tall when positive, else the full chunk grid.
    [[nodiscard]] int effective_tiles_tall() const noexcept {
      return tiles_tall > 0 ? tiles_tall : chunks_tall * chunk_size;
    }

    std::filesystem::path base_dir{}; ///< Directory containing manifest.json.

    /// Returns true if @p c is within [0, chunks_wide) × [0, chunks_tall).
    [[nodiscard]] bool in_bounds(ChunkCoord c) const noexcept {
      return c.col >= 0 && c.col < chunks_wide && c.row >= 0 && c.row < chunks_tall;
    }

    /// Path to the JSON file for chunk @p c.
    [[nodiscard]] std::filesystem::path chunk_path(ChunkCoord c) const;
  };

  /// Parses a manifest.json at @p path.
  /// @return WorldManifest on success, or an error string describing the failure.
  [[nodiscard]] std::expected<WorldManifest, std::string> load_world_manifest(const std::filesystem::path &path);

  /// Converts a Cartesian world-pixel position to the chunk that contains it.
  /// Used internally for collision geometry (which stays in Cartesian space).
  /// @pre tile_px > 0, tile_scale > 0, and @p m has positive chunk_size, chunks_wide and chunks_tall.
  [[nodiscard]] ChunkCoord chunk_at_cart(float wx, float wy, const WorldManifest &m, int tile_px,
                                         float tile_scale) noexcept;

  /// Converts an isometric world-space position to the chunk that contains it.
  /// Use this when the player/entity position is in isometric world space.
  /// The iso position is assumed to be the projection of an entity standing on the
  /// ground (elevation 0); add `elevation * elev_step` to its y before calling if you
  /// have a different elevation in hand (an elevated anchor sits that far above the
  /// ground point being sought).
  /// @pre iso.half_tw > 0, iso.half_th > 0, and @p m has positive chunk_size, chunks_wide and chunks_tall.
  [[nodiscard]] ChunkCoord chunk_at_iso(float iso_x, float iso_y, const WorldManifest &m,
                                        const corundum::core::math::IsometricParams &iso) noexcept;

  /// Top-left Cartesian pixel coordinate of chunk @p c's origin.
  /// Used to offset Cartesian collision rects to absolute world positions.
  [[nodiscard]] std::pair<float, float> chunk_origin_px(ChunkCoord c, const WorldManifest &m, int tile_px,
                                                        float tile_scale) noexcept;

  /// World-space extent of a @p tiles_wide × @p tiles_tall isometric grid, in display pixels.
  ///
  /// Shared by single-map and chunked bounds so the extent formula has one home. Each axis
  /// scales by its own half extent, so a non-square diamond yields distinct width and height.
  /// @pre half_tw > 0 and half_th > 0.
  [[nodiscard]] corundum::world::WorldBounds world_bounds_for_tiles(int tiles_wide, int tiles_tall, float half_tw,
                                                                    float half_th) noexcept;

  /// Total isometric world extent in display pixels (width, height).
  /// Uses the manifest's effective tile counts — tiles_wide/tiles_tall, or the full
  /// chunk grid when those are 0 — and each axis's own half-scale. Width and height are
  /// not generally equal: a diamond tile's screen height is typically half its screen
  /// width (the classic 2:1 isometric ratio), so this does not assume a square bounding
  /// box.
  /// @pre half_tw > 0 and half_th > 0.
  [[nodiscard]] std::pair<float, float> world_bounds_iso(const WorldManifest &m, float half_tw, float half_th) noexcept;

  /// Returns all valid ChunkCoords within @p radius chunks of @p center,
  /// clamped to the world bounds. radius=1 yields up to a 3×3 neighbourhood.
  [[nodiscard]] std::vector<ChunkCoord> active_chunk_coords(ChunkCoord center, int radius, const WorldManifest &m);

} // namespace corundum::world::tilemap
