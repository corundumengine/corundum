#pragma once
#include <cmath>
#include <corundum/core/math/vec.hpp>
#include <cstdint>
#include <flat_map>
#include <limits>
#include <mdspan>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace corundum::gameplay::world::tilemap {

  // Zero-based index into a tileset's flat per-tile arrays (TilesetInfo::tile_rects etc.).
  // 0xFFFF is reserved as "empty / transparent" — renderer skips it.
  using TileId = uint16_t;
  inline constexpr TileId k_empty_tile = 0xFFFF;

  /// Named animation clip defined in a tileset JSON. frames are local tile IDs (indices into the
  /// owning tileset's per-tile arrays).
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

  /// Metadata about the tileset PNG — owned by core, used by platform. Loaded directly from a
  /// spritepacker atlas JSON (schema_version 2): local_id is the tile's index into the atlas's
  /// `sprites[]` array, and every per-tile field below is dense (one entry per local_id, size ==
  /// tile_count) rather than a sparse override on a tileset-wide default — spritepacker already
  /// computes trim and pivot per sprite, so there's no shared default left to override.
  struct TilesetInfo {
    std::string path;
    std::string source;
    int tile_count = 0;

    // Per-tile data, parallel arrays indexed by local_id (size == tile_count).
    std::vector<corundum::core::math::IntRect> tile_rects; ///< Sampled (already-trimmed) rect in the
                                                           ///< atlas texture — ready to use directly
                                                           ///< as a texture source rect.
    std::vector<int> tile_full_width;                      ///< Original (untrimmed) sprite canvas width, from
                                                           ///< spritepacker's source_width. Used to scale pivot into
                                                           ///< pixels, since the trimmed rect alone can't recover it.
    std::vector<int> tile_full_height;                     ///< Same, for source_height.
    std::vector<int> tile_trim_x;                          ///< Offset from the untrimmed canvas's top-left corner to
                                                           ///< tile_rects' content, from spritepacker's trim_x.
    std::vector<int> tile_trim_y;                          ///< Same, for trim_y.
    std::vector<float> tile_pivot_x;                       ///< Anchor point as a fraction of the *full* (untrimmed)
                                                           ///< frame, left = 0. From spritepacker's pivot_x, converted
                                                           ///< from the trimmed-box basis it's authored in.
    std::vector<float> tile_pivot_y;                       ///< Same, for the vertical anchor, bottom = 0 (matching the
                                     ///< isometric ground-contact convention used elsewhere in this
                                     ///< codebase) — inverted from spritepacker's pivot_y, which
                                     ///< measures from the top in raster (image-row) convention.
    std::vector<std::string> tile_names; ///< Sprite name from the atlas; used by tilesmith's palette.

    std::flat_map<std::string, TileAnimation> animations; ///< name → animation
    std::flat_map<int, TileFootprint> tile_footprints;    ///< local_id → footprint; absent = 1×1.
    std::string material; ///< Default terrain-material tag (footstep/ambient audio selection); empty == none.
  };

  /// Bitmask for horizontal flip. Set in TilemapLayer::flip_flags.
  inline constexpr uint8_t k_flip_h = 0x01;
  /// Bitmask for vertical flip. Set in TilemapLayer::flip_flags.
  inline constexpr uint8_t k_flip_v = 0x02;

  /// Axis a ramp/stair tile bridges. Set in TilemapLayer::ramps. Bidirectional along its axis
  /// (walkable both up and down); does nothing for the other axis or the four diagonals.
  enum class RampAxis : uint8_t { NORTH_SOUTH, EAST_WEST };

  /// One layer of tiles. Layers are drawn bottom-to-top; k_empty_tile is transparent.
  /// z_index controls depth relative to entities: 0 = below entities (default), 1+ = above.
  struct TilemapLayer {
    std::string name;
    int z_index = 0;           ///< >= 0; clamped to 0 by loader if negative.
    bool visible = true;       ///< When false, the renderer skips this layer entirely.
    std::vector<TileId> tiles; ///< Row-major, size == map width * height; k_empty_tile for animated cells.
    std::flat_map<int, AnimatedCell> animated_cells; ///< cell_index (row*width+col) → resolved animation.
    std::flat_map<int, uint8_t> flip_flags;          ///< cell_index → k_flip_h | k_flip_v; absent == no flip.
    std::vector<uint8_t> elevation; ///< Per-tile elevation [0–255]; empty == all flat (optional for isometric).
    std::flat_map<int, std::string> material_overrides; ///< cell_index → terrain-material tag, overriding the
                                                        ///< owning tileset's default TilesetInfo::material for
                                                        ///< that one cell (e.g. a snow-dusted patch of an
                                                        ///< otherwise-stone floor); absent == use the tileset
                                                        ///< default.
    std::flat_map<int, RampAxis> ramps; ///< cell_index → axis this ramp tile bridges; absent == not a ramp.
                                        ///< The elevation delta it bridges is inferred from its two
                                        ///< axis-neighbors' elevation_at() values, never stored directly.

    /// Sentinel for baked_animation_index meaning "this cell isn't animated".
    static constexpr uint32_t k_no_animation = std::numeric_limits<uint32_t>::max();

    /// Baked render cache: cell_index → flip bitmask (0 = none). Populated by bake_render_cache();
    /// empty until baked. Lets the per-tile-per-frame render path do a flat array read instead of
    /// a flip_flags.find() (sorted-vector binary search via std::flat_map) every tile every frame.
    std::vector<uint8_t> baked_flip_flags;

    /// Baked render cache: cell_index → index into baked_animations, or k_no_animation. The sparse
    /// animated_cells map is compacted into dense storage at bake time so per-frame lookup is a
    /// flat array read instead of a std::flat_map probe. Populated by bake_render_cache().
    std::vector<uint32_t> baked_animation_index;
    std::vector<AnimatedCell> baked_animations; ///< Dense storage indexed by baked_animation_index.

    /// Tile GID at (col, row); undefined behaviour if out of bounds.
    [[nodiscard]] TileId at(int col, int row, int width) const noexcept {
      return tiles[static_cast<std::size_t>(row * width + col)];
    }

    /// Populates baked_flip_flags / baked_animation_index / baked_animations from flip_flags and
    /// animated_cells. Call once after this layer's tiles/flip_flags/animated_cells are finalized
    /// (see load_tilemap()) — the render hot path (resolve_tile_cell) reads only the baked arrays.
    /// @note The tilesmith editor mutates flip_flags/animated_cells directly and never calls this,
    /// so it reads the flat_maps themselves; re-run this if a loaded Tilemap's layers are ever
    /// mutated and then handed to the game renderer.
    void bake_render_cache(int width, int height) {
      const auto n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

      baked_flip_flags.assign(n, uint8_t{0});
      for (const auto &[idx, flags] : flip_flags)
        if (idx >= 0 && static_cast<std::size_t>(idx) < n)
          baked_flip_flags[static_cast<std::size_t>(idx)] = flags;

      baked_animation_index.assign(n, k_no_animation);
      baked_animations.clear();
      baked_animations.reserve(animated_cells.size());
      for (const auto &[idx, anim] : animated_cells) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= n)
          continue;
        baked_animation_index[static_cast<std::size_t>(idx)] = static_cast<uint32_t>(baked_animations.size());
        baked_animations.push_back(anim);
      }
    }
  };

  /// One tileset entry in a map's tileset table.
  /// first_gid is the GID assigned to local tile 0; tile_count == info.tile_count.
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

  /// A tile's anchor point, as a fraction of its *full* (untrimmed) frame — horizontal measured from
  /// the left edge (0 = left), vertical from the bottom edge (0 = bottom). Sourced from
  /// TilesetInfo::tile_pivot_x/y, which the loader converts from spritepacker's trimmed-box, raster
  /// (top=0) convention into this full-frame, bottom-origin one.
  struct TilePivot {
    float x = 0.5f;
    float y = 0.f;
  };

  /// Returns the pivot for @p local_id in @p info; {0.5, 0} if local_id is out of range.
  [[nodiscard]] inline TilePivot get_tile_pivot(const TilesetInfo &info, int local_id) noexcept {
    if (local_id < 0 || static_cast<std::size_t>(local_id) >= info.tile_pivot_x.size())
      return {};
    const auto i = static_cast<std::size_t>(local_id);
    return {info.tile_pivot_x[i], info.tile_pivot_y[i]};
  }

  /// A tile's placement data relative to its own full (untrimmed) frame: how far the sampled,
  /// already-trimmed tile_rects entry is offset from that frame's top-left corner (trim_x/trim_y),
  /// and the frame's own size (full_width/full_height) — needed to scale TilePivot into pixels,
  /// since the trimmed rect alone can't recover the original canvas size.
  struct TileFrameOffset {
    int trim_x = 0;
    int trim_y = 0;
    int full_width = 0;
    int full_height = 0;
  };

  /// Returns the frame offset/size for @p local_id in @p info; all-zero if local_id is out of range.
  [[nodiscard]] inline TileFrameOffset get_tile_frame_offset(const TilesetInfo &info, int local_id) noexcept {
    if (local_id < 0 || static_cast<std::size_t>(local_id) >= info.tile_trim_x.size())
      return {};
    const auto i = static_cast<std::size_t>(local_id);
    return {info.tile_trim_x[i], info.tile_trim_y[i], info.tile_full_width[i], info.tile_full_height[i]};
  }

  /// Returns the source rect in the tileset texture for @p gid — spritepacker already trims and
  /// packs tightly, so this is a direct array read, not a computed grid cell.
  /// @param ts Tileset entry that owns gid (caller must use find_tileset first).
  /// @param gid Global tile ID.
  /// @return Source rectangle, in pixels, within the tileset texture for this tile; all-zero if gid
  ///         resolves to a local_id outside the tileset's range.
  [[nodiscard]] inline corundum::core::math::IntRect tile_source_rect(const TilemapTileset &ts, TileId gid) noexcept {
    const int local_id = static_cast<int>(gid) - static_cast<int>(ts.first_gid);
    if (local_id < 0 || static_cast<std::size_t>(local_id) >= ts.info.tile_rects.size())
      return {};
    return ts.info.tile_rects[static_cast<std::size_t>(local_id)];
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
    /// Per-rect elevation [0-100], same semantics as TilemapLayer::elevation; empty means
    /// "no elevation data for any rect in this view" and disables elevation filtering entirely.
    std::span<const uint8_t> elevations{};

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
    std::vector<float> cols;         ///< Left edges (tile columns).
    std::vector<float> rows;         ///< Top edges (tile rows).
    std::vector<float> col_spans;    ///< Horizontal extents in tiles.
    std::vector<float> row_spans;    ///< Vertical extents in tiles.
    std::vector<uint8_t> elevations; ///< Per-rect elevation [0-100]; see CollisionRectsView.

    /// Append one collision rect in tile-grid space.
    void push_back(float col, float row, float col_span, float row_span, uint8_t elevation = 0) {
      cols.push_back(col);
      rows.push_back(row);
      col_spans.push_back(col_span);
      row_spans.push_back(row_span);
      elevations.push_back(elevation);
    }

    /// Number of stored rects.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionRectsView view() const noexcept {
      return {cols, rows, col_spans, row_spans, elevations};
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
        elevations[i] = elevations[last];
      }
      cols.pop_back();
      rows.pop_back();
      col_spans.pop_back();
      row_spans.pop_back();
      elevations.pop_back();
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
    /// Per-triangle elevation [0-100]; empty disables elevation filtering. See CollisionRectsView.
    std::span<const uint8_t> elevations{};

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
    std::vector<float> cols;         ///< Left edges (tile columns).
    std::vector<float> rows;         ///< Top edges (tile rows).
    std::vector<float> col_spans;    ///< Horizontal extents in tiles.
    std::vector<float> row_spans;    ///< Vertical extents in tiles.
    std::vector<TriangleCut> cuts;   ///< Which corner is empty.
    std::vector<uint8_t> elevations; ///< Per-triangle elevation [0-100]; see CollisionTrianglesView.

    /// Append one diagonal collision triangle in tile-grid space.
    void push_back(float col, float row, float col_span, float row_span, TriangleCut cut, uint8_t elevation = 0) {
      cols.push_back(col);
      rows.push_back(row);
      col_spans.push_back(col_span);
      row_spans.push_back(row_span);
      cuts.push_back(cut);
      elevations.push_back(elevation);
    }

    /// Number of stored triangles.
    [[nodiscard]] std::size_t size() const noexcept {
      return cols.size();
    }

    /// Non-owning view over the stored data.
    [[nodiscard]] CollisionTrianglesView view() const noexcept {
      return {cols, rows, col_spans, row_spans, cuts, elevations};
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
        elevations[i] = elevations[last];
      }
      cols.pop_back();
      rows.pop_back();
      col_spans.pop_back();
      row_spans.pop_back();
      cuts.pop_back();
      elevations.pop_back();
    }
  };

  /// Static multi-layer tile grid loaded from a JSON map file.
  struct Tilemap {
    std::string path;                     ///< Source file path passed to load_tilemap().
    std::vector<TilemapTileset> tilesets; ///< Sorted ascending by first_gid.
    int width = 0;
    int height = 0;
    int iso_diamond_w = 0; ///< Isometric world step width in pixels (diamond width). 0 = use frame_width.
    int iso_diamond_h = 0; ///< Isometric world step height in pixels (diamond height). 0 = iso_diamond_w / 2.
    std::vector<TilemapLayer> layers;       ///< Index 0 is the bottom-most layer.
    CollisionRects collisions;              ///< Impassable rects in Tiled pixel space; empty if absent.
    CollisionTriangles collision_triangles; ///< Diagonal half-tile collision shapes; empty if absent.

    /// Effective isometric diamond width (world step), falling back to the first tileset's first
    /// tile's full frame width if not set.
    [[nodiscard]] int diamond_w() const noexcept {
      if (iso_diamond_w > 0)
        return iso_diamond_w;
      if (tilesets.empty() || tilesets[0].info.tile_full_width.empty())
        return 0;
      return tilesets[0].info.tile_full_width[0];
    }

    /// Effective isometric diamond height, falling back to diamond_w / 2 if not set.
    [[nodiscard]] int diamond_h() const noexcept {
      if (iso_diamond_h > 0)
        return iso_diamond_h;
      return diamond_w() / 2;
    }

    /// 2D mdspan view over @p layer's tile grid — rows × cols layout. Lives on Tilemap
    /// (not TilemapLayer) since width/height are Tilemap-owned; layer doesn't self-construct.
    /// @pre &layer must belong to this Tilemap's layers, and layer.tiles.size() == width * height.
    [[nodiscard]] auto layer_view(TilemapLayer &layer) noexcept {
      return std::mdspan<TileId, std::dextents<std::size_t, 2>>(layer.tiles.data(), static_cast<std::size_t>(height),
                                                                static_cast<std::size_t>(width));
    }

    /// Const overload of layer_view().
    [[nodiscard]] auto layer_view(const TilemapLayer &layer) const noexcept {
      return std::mdspan<const TileId, std::dextents<std::size_t, 2>>(
          layer.tiles.data(), static_cast<std::size_t>(height), static_cast<std::size_t>(width));
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
   * @brief Ramp axis of the tile at (col, row), if it's a ramp.
   *
   * Same topmost-z_index==0-layer-with-a-tile resolution convention as elevation_at().
   *
   * @param tm  The tilemap to query.
   * @param col Column index (0-based).
   * @param row Row index (0-based).
   * @return The axis this cell bridges, or std::nullopt if out of bounds, no z_index==0 layer
   *         has a tile there, or the cell isn't a ramp.
   */
  [[nodiscard]] inline std::optional<RampAxis> ramp_axis_at(const Tilemap &tm, int col, int row) noexcept {
    if (col < 0 || row < 0 || col >= tm.width || row >= tm.height)
      return std::nullopt;
    const std::size_t uidx =
        static_cast<std::size_t>(row) * static_cast<std::size_t>(tm.width) + static_cast<std::size_t>(col);
    std::optional<RampAxis> result;
    for (const auto &layer : tm.layers) {
      if (layer.z_index != 0 || !layer.visible)
        continue;
      const int idx = static_cast<int>(uidx);
      const bool has_tile =
          layer.animated_cells.contains(idx) || (uidx < layer.tiles.size() && layer.tiles[uidx] != k_empty_tile);
      if (!has_tile)
        continue;
      if (auto it = layer.ramps.find(idx); it != layer.ramps.end())
        result = it->second;
      else
        result = std::nullopt;
    }
    return result;
  }

  /**
   * @brief Smoothly-interpolated elevation at a fractional world position — used for the
   * entity height lift so crossing a ramp doesn't pop between its two flanking elevations.
   *
   * Falls back to plain elevation_at() (floored) for any non-ramp cell, so behavior for the
   * rest of the map is unchanged. Single-map mode only — chunked/streamed World mode has no
   * caller for this yet and keeps the discrete per-tile elevation_at() lift.
   *
   * @param tm    The tilemap to query.
   * @param col_f Fractional column (e.g. an entity's world position in tile-grid units).
   * @param row_f Fractional row.
   * @return Elevation, linearly interpolated across a ramp cell along its axis.
   */
  [[nodiscard]] inline float interpolated_elevation_at(const Tilemap &tm, float col_f, float row_f) noexcept {
    const int col = static_cast<int>(std::floor(col_f));
    const int row = static_cast<int>(std::floor(row_f));
    const std::optional<RampAxis> axis = ramp_axis_at(tm, col, row);
    if (!axis)
      return static_cast<float>(elevation_at(tm, col, row));
    if (*axis == RampAxis::NORTH_SOUTH) {
      const float t = row_f - static_cast<float>(row); // 0 at the north edge, 1 at the south edge
      return std::lerp(static_cast<float>(elevation_at(tm, col, row - 1)),
                       static_cast<float>(elevation_at(tm, col, row + 1)), t);
    }
    const float t = col_f - static_cast<float>(col); // 0 at the west edge, 1 at the east edge
    return std::lerp(static_cast<float>(elevation_at(tm, col - 1, row)),
                     static_cast<float>(elevation_at(tm, col + 1, row)), t);
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
