#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace corundum::gameplay::world::tilemap {

  /// Grid coordinate identifying a single chunk in the world map (column, row).
  struct ChunkCoord {
    int col = 0; ///< Column (west→east).
    int row = 0; ///< Row (north→south).
  };

  [[nodiscard]] inline bool operator==(ChunkCoord a, ChunkCoord b) noexcept {
    return a.col == b.col && a.row == b.row;
  }

  /// Parsed world manifest — describes the chunk grid layout.
  struct WorldManifest {
    int chunk_size = 0;  ///< Tiles per chunk side (128).
    int chunks_wide = 0; ///< Number of chunks along X (16).
    int chunks_tall = 0; ///< Number of chunks along Y (8).

    /// Effective tile columns / rows that actually contain non-empty tiles.
    /// When both are 0 the game falls back to chunks_wide × chunk_size
    /// (i.e. the full grid — no trimming needed).
    int tiles_wide = 0;
    int tiles_tall = 0;

    std::filesystem::path base_dir; ///< Directory containing manifest.json.

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
  /// @pre tile_px > 0 and tile_scale > 0.
  [[nodiscard]] ChunkCoord chunk_at_cart(float wx, float wy, const WorldManifest &m, int tile_px,
                                         float tile_scale) noexcept;

  /// Converts an isometric world-space position to the chunk that contains it.
  /// Use this when the player/entity position is in isometric world space.
  /// @pre half_tw > 0 and half_th > 0.
  [[nodiscard]] ChunkCoord chunk_at_iso(float iso_x, float iso_y, const WorldManifest &m, float half_tw,
                                        float half_th) noexcept;

  /// Top-left Cartesian pixel coordinate of chunk @p c's origin.
  /// Used to offset Cartesian collision rects to absolute world positions.
  [[nodiscard]] std::pair<float, float> chunk_origin_px(ChunkCoord c, const WorldManifest &m, int tile_px,
                                                        float tile_scale) noexcept;

  /// Total isometric world extent in display pixels (width, height).
  /// Width  = (tiles_wide + tiles_tall - 1) * half_tw * 2.
  /// Height = (tiles_wide + tiles_tall - 1) * half_th * 2.
  /// Not generally equal — a diamond tile's screen height is typically half its
  /// screen width (the classic 2:1 isometric ratio), so this uses each axis's own
  /// scale factor rather than assuming a square bounding box.
  /// @pre half_tw > 0 and half_th > 0.
  [[nodiscard]] std::pair<float, float> world_bounds_iso(const WorldManifest &m, float half_tw, float half_th) noexcept;

  /// Returns all valid ChunkCoords within @p radius chunks of @p center,
  /// clamped to the world bounds. radius=1 yields up to a 3×3 neighbourhood.
  [[nodiscard]] std::vector<ChunkCoord> active_chunk_coords(ChunkCoord center, int radius, const WorldManifest &m);

} // namespace corundum::gameplay::world::tilemap
