#pragma once
#include <corundum/core/math/vec.hpp>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <mdspan>
#include <span>
#include <string>
#include <vector>

namespace corundum::gameplay::world::tilemap {

  // Zero-based index into the tileset grid (col = id % columns, row = id / columns).
  // 0xFFFF is reserved as "empty / transparent" — renderer skips it.
  using TileId = uint16_t;
  inline constexpr TileId k_empty_tile = 0xFFFF;

  /// Named animation clip defined in a tileset JSON.
  /// frames are local tile IDs (column-major index within the tileset PNG grid).
  struct TileAnimation {
    std::vector<int> frames;
    float fps = 5.f;
  };

  /// Resolved animated cell stored in a layer after load.
  /// frame_gids are global tile IDs, ready for direct use with get_tile_texture.
  struct AnimatedCell {
    std::string anim_name; ///< Original clip name, used when saving.
    std::vector<TileId> frame_gids;
    float fps = 5.f;
  };

  /// Tile footprint in grid units. Defaults to 1×1 when absent.
  struct TileFootprint {
    int w = 1; ///< Width in tiles (>= 1).
    int h = 1; ///< Height in tiles (>= 1).
  };

  /// Metadata about the tileset PNG — owned by core, used by platform.
  struct TilesetInfo {
    std::string path;
    std::string source;
    int tile_width = 0;
    int tile_height = 0;
    int columns = 0;
    int rows = 0;
    float pivot_x = 0.f; ///< Horizontal pivot fraction [0, 1]; 0 = left edge. Applied when drawing sprite.
    float pivot_y = 0.f; ///< Vertical pivot fraction [0, 1] measured from the BOTTOM (Unity convention).
    std::flat_map<std::string, TileAnimation> animations; ///< name → animation
    std::flat_map<int, TileFootprint> tile_footprints;    ///< local_id → footprint; absent = 1×1.
    std::string material; ///< Default terrain-material tag (footstep/ambient audio selection); empty == none.
  };

  /// Bitmask for horizontal flip. Set in TilemapLayer::flip_flags.
  inline constexpr uint8_t k_flip_h = 0x01;
  /// Bitmask for vertical flip. Set in TilemapLayer::flip_flags.
  inline constexpr uint8_t k_flip_v = 0x02;

  /// One layer of tiles. Layers are drawn bottom-to-top; k_empty_tile is transparent.
  /// z_index controls depth relative to entities: 0 = below entities (default), 1+ = above.
  struct TilemapLayer {
    std::string name;
    int z_index = 0;           ///< >= 0; clamped to 0 by loader if negative.
    bool visible = true;       ///< When false, the renderer skips this layer entirely.
    std::vector<TileId> tiles; ///< Row-major, size == map width * height; k_empty_tile for animated cells.
    std::flat_map<int, AnimatedCell> animated_cells; ///< cell_index (row*width+col) → resolved animation.
    std::flat_map<int, uint8_t> flip_flags;          ///< cell_index → k_flip_h | k_flip_v; absent == no flip.
    std::vector<uint8_t> elevation; ///< Per-tile elevation [0–100]; empty == all flat (optional for isometric).
    std::flat_map<int, std::string> material_overrides; ///< cell_index → terrain-material tag, overriding the
                                                        ///< owning tileset's default TilesetInfo::material for
                                                        ///< that one cell (e.g. a snow-dusted patch of an
                                                        ///< otherwise-stone floor); absent == use the tileset
                                                        ///< default.

    /// 2D mdspan view over the tile grid — rows × cols layout.
    /// @pre tiles.size() == width * height
    [[nodiscard]] auto view(int width, int height) noexcept {
      return std::mdspan<TileId, std::dextents<int, 2>>(tiles.data(), height, width);
    }

    /// Const overload of view().
    [[nodiscard]] auto view(int width, int height) const noexcept {
      return std::mdspan<const TileId, std::dextents<int, 2>>(tiles.data(), height, width);
    }

    /// Tile GID at (col, row); undefined behaviour if out of bounds.
    [[nodiscard]] TileId at(int col, int row, int width) const noexcept {
      return tiles[static_cast<std::size_t>(row * width + col)];
    }
  };

  /// One tileset entry in a map's tileset table.
  /// first_gid is the GID assigned to local tile 0; tile_count = columns * rows.
  struct TilemapTileset {
    TilesetInfo info;
    TileId first_gid = 0;
    int tile_count = 0;
  };

  /// Returns the TilemapTileset that owns gid, or nullptr if gid is k_empty_tile
  /// or falls outside every tileset's range. tilesets must be sorted ascending
  /// by first_gid (guaranteed by the loader).
  [[nodiscard]] inline const TilemapTileset *find_tileset(const std::vector<TilemapTileset> &tilesets,
                                                          TileId gid) noexcept {
    if (gid == k_empty_tile)
      return nullptr;
    const TilemapTileset *result = nullptr;
    for (const auto &ts : tilesets) {
      if (ts.first_gid <= gid)
        result = &ts;
      else
        break;
    }
    if (!result)
      return nullptr;
    const int local_id = static_cast<int>(gid) - static_cast<int>(result->first_gid);
    if (local_id >= result->tile_count)
      return nullptr;
    return result;
  }

  /// Returns the footprint for @p local_id in @p info, defaulting to {1, 1} when absent.
  [[nodiscard]] inline TileFootprint get_tile_footprint(const TilesetInfo &info, int local_id) noexcept {
    if (auto it = info.tile_footprints.find(local_id); it != info.tile_footprints.end())
      return it->second;
    return {};
  }

  /// Returns the footprint for @p gid, resolved through @p tilesets. Returns {1, 1} for unknown GIDs.
  [[nodiscard]] inline TileFootprint get_tile_footprint(const std::vector<TilemapTileset> &tilesets,
                                                        TileId gid) noexcept {
    const TilemapTileset *ts = find_tileset(tilesets, gid);
    if (!ts)
      return {};
    return get_tile_footprint(ts->info, static_cast<int>(gid) - static_cast<int>(ts->first_gid));
  }

  /// Returns the source rect in the tileset texture for @p gid.
  /// @param ts Tileset entry that owns gid (caller must use find_tileset first).
  /// @param gid Global tile ID.
  /// @return Source rectangle, in pixels, within the tileset texture for this tile.
  /// @pre ts.info.columns > 0
  [[nodiscard]] inline corundum::core::math::IntRect tile_source_rect(const TilemapTileset &ts, TileId gid) noexcept {
    const int local_id = static_cast<int>(gid) - static_cast<int>(ts.first_gid);
    const int src_col = local_id % ts.info.columns;
    const int src_row = local_id / ts.info.columns;
    return {src_col * ts.info.tile_width, src_row * ts.info.tile_height, ts.info.tile_width, ts.info.tile_height};
  }

  /// Axis-aligned rectangle in tile-grid space.
  /// Used as a temporary during loading; runtime collision uses CollisionRects (SoA).
  struct CollisionRect {
    float col = 0.f;
    float row = 0.f;
    float col_span = 0.f;
    float row_span = 0.f;
  };

  /**
   * @brief Non-owning Structure-of-Arrays view over collision rect data in tile-grid space.
   *
   * Passed to collision functions. Enables SIMD auto-vectorization by
   * exposing each field as a contiguous span rather than interleaved structs.
   * Construct from CollisionRects::view() or directly from stack arrays.
   */
  struct CollisionRectsView {
    std::span<const float> cols;      ///< Left edges (tile columns).
    std::span<const float> rows;      ///< Top edges (tile rows).
    std::span<const float> col_spans; ///< Horizontal extents in tiles.
    std::span<const float> row_spans; ///< Vertical extents in tiles.

    /// Number of rects in the view.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }
  };

  /**
   * @brief Owned Structure-of-Arrays storage for collision rects in tile-grid space.
   *
   * Stores rect fields in four parallel vectors so that overlaps_any() can
   * iterate each field as a contiguous float array, enabling auto-vectorization.
   * Populated once at load time; no per-frame allocation.
   *
   * @see CollisionRectsView for the non-owning counterpart passed to collision functions.
   */
  struct CollisionRects {
    std::vector<float> cols;      ///< Left edges (tile columns).
    std::vector<float> rows;      ///< Top edges (tile rows).
    std::vector<float> col_spans; ///< Horizontal extents in tiles.
    std::vector<float> row_spans; ///< Vertical extents in tiles.

    /// Append one collision rect in tile-grid space.
    void push_back(float col, float row, float col_span, float row_span) {
      cols.push_back(col);
      rows.push_back(row);
      col_spans.push_back(col_span);
      row_spans.push_back(row_span);
    }

    /// Number of stored rects.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionRectsView view() const noexcept {
      return {cols, rows, col_spans, row_spans};
    }

    /// Remove the rect at index @p i in O(1) via swap-and-pop. Order is not preserved.
    /// @pre i < size()
    void erase(std::size_t i) {
      const std::size_t last = size() - 1;
      if (i != last) {
        cols[i] = cols[last];
        rows[i] = rows[last];
        col_spans[i] = col_spans[last];
        row_spans[i] = row_spans[last];
      }
      cols.pop_back();
      rows.pop_back();
      col_spans.pop_back();
      row_spans.pop_back();
    }
  };

  /**
   * @brief Identifies which corner of a tile is the empty (non-solid) region.
   *
   * The solid region is the triangle that does NOT contain the named corner.
   * NW/SE triangles share the TR→BL hypotenuse; NE/SW share the TL→BR hypotenuse.
   */
  enum class TriangleCut : uint8_t { NW, NE, SW, SE };

  /// Non-owning Structure-of-Arrays view over diagonal collision triangle data in tile-grid space.
  struct CollisionTrianglesView {
    std::span<const float> cols;       ///< Left edges (tile columns).
    std::span<const float> rows;       ///< Top edges (tile rows).
    std::span<const float> col_spans;  ///< Horizontal extents in tiles.
    std::span<const float> row_spans;  ///< Vertical extents in tiles.
    std::span<const TriangleCut> cuts; ///< Which corner is empty.

    /// Number of triangles in the view.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }
  };

  /**
   * @brief Owned Structure-of-Arrays storage for diagonal half-tile collision shapes in tile-grid space.
   *
   * Each entry is a right-isoceles triangle occupying half a tile. The empty corner
   * is identified by TriangleCut; the other half is solid.
   */
  struct CollisionTriangles {
    std::vector<float> cols;       ///< Left edges (tile columns).
    std::vector<float> rows;       ///< Top edges (tile rows).
    std::vector<float> col_spans;  ///< Horizontal extents in tiles.
    std::vector<float> row_spans;  ///< Vertical extents in tiles.
    std::vector<TriangleCut> cuts; ///< Which corner is empty.

    /// Append one diagonal collision triangle in tile-grid space.
    void push_back(float col, float row, float col_span, float row_span, TriangleCut cut) {
      cols.push_back(col);
      rows.push_back(row);
      col_spans.push_back(col_span);
      row_spans.push_back(row_span);
      cuts.push_back(cut);
    }

    /// Number of stored triangles.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionTrianglesView view() const noexcept {
      return {cols, rows, col_spans, row_spans, cuts};
    }

    /// Remove the triangle at index @p i in O(1) via swap-and-pop. Order is not preserved.
    /// @pre i < size()
    void erase(std::size_t i) {
      const std::size_t last = size() - 1;
      if (i != last) {
        cols[i] = cols[last];
        rows[i] = rows[last];
        col_spans[i] = col_spans[last];
        row_spans[i] = row_spans[last];
        cuts[i] = cuts[last];
      }
      cols.pop_back();
      rows.pop_back();
      col_spans.pop_back();
      row_spans.pop_back();
      cuts.pop_back();
    }
  };

  /// Static multi-layer tile grid loaded from a JSON map file.
  struct Tilemap {
    std::string path;                     ///< Source file path passed to load_tilemap().
    std::vector<TilemapTileset> tilesets; ///< Sorted ascending by first_gid.
    int width = 0;
    int height = 0;
    int iso_diamond_w = 0; ///< Isometric world step width in pixels (diamond width). 0 = use tile_width.
    int iso_diamond_h = 0; ///< Isometric world step height in pixels (diamond height). 0 = iso_diamond_w / 2.
    std::vector<TilemapLayer> layers;       ///< Index 0 is the bottom-most layer.
    CollisionRects collisions;              ///< Impassable rects in Tiled pixel space; empty if absent.
    CollisionTriangles collision_triangles; ///< Diagonal half-tile collision shapes; empty if absent.

    /// Effective isometric diamond width (world step), falling back to tile_width if not set.
    [[nodiscard]] int diamond_w() const noexcept {
      if (iso_diamond_w > 0)
        return iso_diamond_w;
      return tilesets.empty() ? 0 : tilesets[0].info.tile_width;
    }

    /// Effective isometric diamond height, falling back to diamond_w / 2 if not set.
    [[nodiscard]] int diamond_h() const noexcept {
      if (iso_diamond_h > 0)
        return iso_diamond_h;
      return diamond_w() / 2;
    }
  };

  /**
   * @brief Elevation of the tile at (col, row), for entities standing on it.
   *
   * A cell can carry independent elevation data on more than one z_index==0
   * layer; this resolves to the last (topmost-painted) such layer that has a
   * tile present at the cell — a deliberate convention, since no single
   * canonical "ground layer" is otherwise designated.
   *
   * @return Elevation [0-255], or 0 if out of bounds, no z_index==0 layer has
   *         a tile there, or the layer has no elevation data.
   */
  [[nodiscard]] inline int elevation_at(const Tilemap &tm, int col, int row) noexcept {
    if (col < 0 || row < 0 || col >= tm.width || row >= tm.height)
      return 0;
    const auto uidx =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(tm.width) + static_cast<std::size_t>(col);
    int result = 0;
    for (const auto &layer : tm.layers) {
      if (layer.z_index != 0 || !layer.visible)
        continue;
      if (uidx >= layer.elevation.size())
        continue;
      const int idx = static_cast<int>(uidx);
      const bool has_tile =
          layer.animated_cells.contains(idx) || (uidx < layer.tiles.size() && layer.tiles[uidx] != k_empty_tile);
      if (!has_tile)
        continue;
      result = layer.elevation[uidx];
    }
    return result;
  }

  /**
   * @brief Terrain-material tag of the tile at (col, row), for footstep/ambient audio selection.
   *
   * Resolves a per-cell material_overrides entry on the topmost z_index==0 layer with a tile
   * present at the cell (same layer-precedence convention as elevation_at); if that cell has no
   * override, falls back to the owning tileset's default TilesetInfo::material. This keeps the
   * common case (every tile of a terrain type sharing one material) free, while still letting an
   * individual placed tile diverge from its tileset's default (e.g. a snow-dusted patch of an
   * otherwise-stone floor) without deriving the tag from the tile's visual art index.
   *
   * @param tm  The tilemap to query.
   * @param col Column index (0-based, left to right).
   * @param row Row index (0-based, top to bottom).
   * @return The resolved material tag, or an empty string if out of bounds, no z_index==0 layer has
   *         a tile there, or neither an override nor a tileset default is set.
   */
  [[nodiscard]] inline std::string material_at(const Tilemap &tm, int col, int row) {
    if (col < 0 || row < 0 || col >= tm.width || row >= tm.height)
      return {};
    const std::size_t uidx =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(tm.width) + static_cast<std::size_t>(col);
    if (uidx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return {};
    const int idx = static_cast<int>(uidx);
    std::string result;
    for (const auto &layer : tm.layers) {
      if (layer.z_index != 0 || !layer.visible)
        continue;
      if (uidx >= layer.tiles.size())
        continue;
      const bool is_animated = layer.animated_cells.contains(idx);
      if (!is_animated && layer.tiles[uidx] == k_empty_tile)
        continue;

      if (auto it = layer.material_overrides.find(idx); it != layer.material_overrides.end()) {
        result = it->second;
        continue;
      }
      const TileId gid =
          is_animated ? (layer.animated_cells.at(idx).frame_gids.empty() ? k_empty_tile
                                                                         : layer.animated_cells.at(idx).frame_gids[0])
                      : layer.tiles[uidx];
      if (const TilemapTileset *ts = find_tileset(tm.tilesets, gid); ts != nullptr)
        result = ts->info.material;
    }
    return result;
  }

  /**
   * @brief Validates a tilemap for author-time errors: orphaned tile references, duplicate layer
   * names, and out-of-bounds collision geometry.
   *
   * Intended to be run explicitly before save/export (e.g. by an editor), not as a load-time gate —
   * @see corundum::gameplay::world::tilemap::load_tilemap for load-time structural checks.
   *
   * @param tm The tilemap to validate.
   * @return One human-readable message per problem found, each naming the offending layer/cell/rect
   *         so an editor can point the author at a specific location. Empty if the map is valid.
   */
  [[nodiscard]] std::vector<std::string> validate(const Tilemap &tm);

} // namespace corundum::gameplay::world::tilemap
