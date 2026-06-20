#pragma once
#include <cstdint>
#include <flat_map>
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
    std::string path;   ///< e.g. "game/assets/textures/tileset.png"
    std::string source; ///< Path to the source JSON file, e.g. "game/data/sprite_sheets/objects/terrain.json"
    int tile_width = 0;
    int tile_height = 0;
    int columns = 0;
    int rows = 0;
    float pivot_x = 0.f; ///< Horizontal pivot fraction [0, 1]; 0 = left edge. Applied when drawing sprite.
    float pivot_y = 0.f; ///< Vertical pivot fraction [0, 1] measured from the BOTTOM (Unity convention).
    std::flat_map<std::string, TileAnimation> animations; ///< name → animation
    std::flat_map<int, TileFootprint> tile_footprints;    ///< local_id → footprint; absent = 1×1.
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

  /// Axis-aligned rectangle in Tiled pixel space (unscaled).
  /// Used as a temporary during loading; runtime collision uses CollisionRects (SoA).
  struct CollisionRect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
  };

  /**
   * @brief Non-owning Structure-of-Arrays view over collision rect data.
   *
   * Passed to collision functions. Enables SIMD auto-vectorization by
   * exposing each field as a contiguous span rather than interleaved structs.
   * Construct from CollisionRects::view() or directly from stack arrays.
   */
  struct CollisionRectsView {
    std::span<const float> xs; ///< Left edges.
    std::span<const float> ys; ///< Top edges.
    std::span<const float> ws; ///< Widths.
    std::span<const float> hs; ///< Heights.

    /// Number of rects in the view.
    [[nodiscard]] std::size_t size() const noexcept {
      return xs.size();
    }
  };

  /**
   * @brief Owned Structure-of-Arrays storage for axis-aligned collision rects.
   *
   * Stores rect fields in four parallel vectors so that overlaps_any() can
   * iterate each field as a contiguous float array, enabling auto-vectorization.
   * Populated once at load time; no per-frame allocation.
   *
   * @see CollisionRectsView for the non-owning counterpart passed to collision functions.
   */
  struct CollisionRects {
    std::vector<float> xs; ///< Left edges.
    std::vector<float> ys; ///< Top edges.
    std::vector<float> ws; ///< Widths.
    std::vector<float> hs; ///< Heights.

    /// Append one collision rect.
    void push_back(float x, float y, float w, float h) {
      xs.push_back(x);
      ys.push_back(y);
      ws.push_back(w);
      hs.push_back(h);
    }

    /**
     * @brief Multiply all coordinates and dimensions by @p factor in-place.
     * @param factor Scale multiplier (e.g. tile_scale to convert Tiled → world pixels).
     */
    void scale(float factor) noexcept {
      for (float &v : xs)
        v *= factor;
      for (float &v : ys)
        v *= factor;
      for (float &v : ws)
        v *= factor;
      for (float &v : hs)
        v *= factor;
    }

    /// Number of stored rects.
    [[nodiscard]] std::size_t size() const noexcept {
      return xs.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionRectsView view() const noexcept {
      return {xs, ys, ws, hs};
    }

    /// Remove the rect at index @p i in O(1) via swap-and-pop. Order is not preserved.
    /// @pre i < size()
    void erase(std::size_t i) {
      const std::size_t last = size() - 1;
      if (i != last) {
        xs[i] = xs[last];
        ys[i] = ys[last];
        ws[i] = ws[last];
        hs[i] = hs[last];
      }
      xs.pop_back();
      ys.pop_back();
      ws.pop_back();
      hs.pop_back();
    }
  };

  /**
   * @brief Identifies which corner of a tile is the empty (non-solid) region.
   *
   * The solid region is the triangle that does NOT contain the named corner.
   * NW/SE triangles share the TR→BL hypotenuse; NE/SW share the TL→BR hypotenuse.
   */
  enum class TriangleCut : uint8_t { NW, NE, SW, SE };

  /// Non-owning Structure-of-Arrays view over diagonal collision triangle data.
  struct CollisionTrianglesView {
    std::span<const float> xs;         ///< Left edges.
    std::span<const float> ys;         ///< Top edges.
    std::span<const float> ws;         ///< Widths.
    std::span<const float> hs;         ///< Heights.
    std::span<const TriangleCut> cuts; ///< Which corner is empty.

    /// Number of triangles in the view.
    [[nodiscard]] std::size_t size() const noexcept {
      return xs.size();
    }
  };

  /**
   * @brief Owned Structure-of-Arrays storage for diagonal half-tile collision shapes.
   *
   * Each entry is a right-isoceles triangle occupying half a tile. The empty corner
   * is identified by TriangleCut; the other half is solid.
   */
  struct CollisionTriangles {
    std::vector<float> xs;         ///< Left edges.
    std::vector<float> ys;         ///< Top edges.
    std::vector<float> ws;         ///< Widths.
    std::vector<float> hs;         ///< Heights.
    std::vector<TriangleCut> cuts; ///< Which corner is empty.

    /// Append one diagonal collision triangle.
    void push_back(float x, float y, float w, float h, TriangleCut cut) {
      xs.push_back(x);
      ys.push_back(y);
      ws.push_back(w);
      hs.push_back(h);
      cuts.push_back(cut);
    }

    /**
     * @brief Multiply all coordinates and dimensions by @p factor in-place.
     * @param factor Scale multiplier (e.g. tile_scale to convert Tiled → world pixels).
     */
    void scale(float factor) noexcept {
      for (float &v : xs)
        v *= factor;
      for (float &v : ys)
        v *= factor;
      for (float &v : ws)
        v *= factor;
      for (float &v : hs)
        v *= factor;
    }

    /// Number of stored triangles.
    [[nodiscard]] std::size_t size() const noexcept {
      return xs.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionTrianglesView view() const noexcept {
      return {xs, ys, ws, hs, cuts};
    }

    /// Remove the triangle at index @p i in O(1) via swap-and-pop. Order is not preserved.
    /// @pre i < size()
    void erase(std::size_t i) {
      const std::size_t last = size() - 1;
      if (i != last) {
        xs[i] = xs[last];
        ys[i] = ys[last];
        ws[i] = ws[last];
        hs[i] = hs[last];
        cuts[i] = cuts[last];
      }
      xs.pop_back();
      ys.pop_back();
      ws.pop_back();
      hs.pop_back();
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

} // namespace corundum::gameplay::world::tilemap
