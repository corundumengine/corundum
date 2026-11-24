// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <corundum/core/math/vec.hpp>
#include <cstddef>
#include <cstdint>
#include <flat_map>
#include <functional>
#include <limits>
#include <mdspan>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace corundum::world::tilemap {

/** Zero-based index into a tileset's flat per-tile arrays (TilesetInfo::tile_rects etc.).
 *  0xFFFF is reserved as "empty / transparent" — renderer skips it.
 */
using TileId = uint16_t;
inline constexpr TileId k_empty_tile = 0xFFFF;

/** Named animation clip defined in a tileset JSON. frames are local tile IDs (indices into the
 *  owning tileset's per-tile arrays).
 */
struct TileAnimation {
  std::vector<int> frames;

  float fps = 5.f;
};

/** Resolved animated cell stored in a layer after load.
 *  frame_gids are global tile IDs, ready for direct use with get_tile_texture.
 */
struct AnimatedCell {
  /** Original clip name, used when saving. */
  std::string anim_name;

  std::vector<TileId> frame_gids;

  float fps = 5.f;
};

/** Metadata about the tileset PNG — owned by core, used by platform. Loaded directly from a
 *  spritepacker atlas JSON (schema_version 2): local_id is the tile's index into the atlas's
 *  `sprites[]` array, and every per-tile field below is dense (one entry per local_id, size ==
 *  tile_count) rather than a sparse override on a tileset-wide default — spritepacker already
 *  computes trim and pivot per sprite, so there's no shared default left to override.
 */
struct TilesetInfo {
  std::string path;

  std::string source;

  int tile_count = 0;

  // Per-tile data, parallel arrays indexed by local_id (size == tile_count).

  /** Sampled (already-trimmed) rect in the atlas texture — ready to use directly as a texture source rect. */
  std::vector<corundum::core::math::IntRect> tile_rects;

  /** Original (untrimmed) sprite canvas width, from spritepacker's source_width. Used to scale pivot into pixels, since
   *  the trimmed rect alone can't recover it.
   */
  std::vector<int> tile_full_width;

  /** Same, for source_height. */
  std::vector<int> tile_full_height;

  /** Offset from the untrimmed canvas's top-left corner to tile_rects' content, from spritepacker's trim_x. */
  std::vector<int> tile_trim_x;

  /** Same, for trim_y. */
  std::vector<int> tile_trim_y;

  /** Anchor point as a fraction of the *full* (untrimmed) frame, left = 0. From spritepacker's pivot_x, converted from
   *  the trimmed-box basis it's authored in.
   */
  std::vector<float> tile_pivot_x;

  /** Same, for the vertical anchor, bottom = 0 (matching the isometric ground-contact convention used elsewhere in this
   *  codebase) — inverted from spritepacker's pivot_y, which measures from the top in raster (image-row) convention.
   */
  std::vector<float> tile_pivot_y;

  /** Sprite name from the atlas; used by tilesmith's palette. */
  std::vector<std::string> tile_names;

  /** name → animation */
  std::flat_map<std::string, TileAnimation> animations;

  /** Default terrain-material tag (footstep/ambient audio selection); empty == none. */
  std::string material;
};

/** Bitmask for horizontal flip. Set in TilemapLayer::flip_flags. */
inline constexpr uint8_t k_flip_h = 0x01;

/** Bitmask for vertical flip. Set in TilemapLayer::flip_flags. */
inline constexpr uint8_t k_flip_v = 0x02;

/** Axis a ramp/stair tile bridges. Set in TilemapLayer::ramps. Bidirectional along its axis
 *  (walkable both up and down); does nothing for the other axis or the four diagonals.
 */
enum class RampAxis : uint8_t { NorthSouth, EastWest };

/** Row-major index of cell (`col`, `row`) in a grid `width` cells wide: the one place the
 *  layout of tiles, elevation and every other per-cell array is spelled.
 *  @pre 0 <= col < width and 0 <= row.
 */
[[nodiscard]] constexpr std::size_t cell_index(int width, int col, int row) noexcept {
  assert(0 <= col && col < width && 0 <= row);
  return (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(col);
}

/** One layer of tiles. Layers are drawn bottom-to-top; k_empty_tile is transparent.
 *  z_index controls depth relative to entities: 0 = below entities (default), 1+ = above.
 *  A z_index > 0 layer with depth_sorted = true is instead depth-sorted per-tile against
 *  entities (see TilemapLayer::depth_sorted).
 */
struct TilemapLayer {
  std::string name;

  /** >= 0; clamped to 0 by loader if negative. */
  int z_index = 0;

  /** Only meaningful when z_index > 0. false (default): layer always draws above all entities (rooftops, canopy,
   *  decals). true: layer's tiles are depth-sorted against entities and elevated ground tiles via iso_depth_key instead
   *  (walls, pillars, other tall props players walk around/behind).
   */
  bool depth_sorted = false;

  /** Row-major, size == map width * height; k_empty_tile for animated cells. */
  std::vector<TileId> tiles;

  /** cell_index (row*width+col) → resolved animation. */
  std::flat_map<int, AnimatedCell> animated_cells;

  /** cell_index → k_flip_h | k_flip_v; absent == no flip. */
  std::flat_map<int, uint8_t> flip_flags;

  /** Per-tile elevation [0–255]; empty == all flat (optional for isometric). */
  std::vector<uint8_t> elevation;

  /** cell_index → terrain-material tag, overriding the owning tileset's default TilesetInfo::material for that one cell
   *  (e.g. a snow-dusted patch of an otherwise-stone floor); absent == use the tileset default.
   */
  std::flat_map<int, std::string> material_overrides;

  /** cell_index → axis this ramp tile bridges; absent == not a ramp. The elevation delta it bridges is inferred from
   *  its two axis-neighbors' elevation_at() values, never stored directly.
   */
  std::flat_map<int, RampAxis> ramps;

  /** Tile GID at (col, row); undefined behaviour if out of bounds. Prefer Tilemap::tile_at(). */
  [[nodiscard]] TileId tile_at_unchecked(int col, int row, int width) const noexcept {
    return tiles[cell_index(width, col, row)];
  }
};

/** One tileset entry in a map's tileset table.
 *  first_gid is the GID assigned to local tile 0; the tile count lives on info.
 */
struct TilemapTileset {
  TilesetInfo info;

  TileId first_gid = 0;
};

/** Returns the TilemapTileset that owns gid, or nullptr if gid is k_empty_tile
 *  or falls outside every tileset's range. tilesets must be sorted ascending
 *  by first_gid (guaranteed by the loader).
 */
[[nodiscard]] inline const TilemapTileset *find_tileset(const std::vector<TilemapTileset> &tilesets,
                                                        TileId gid) noexcept {
  if (gid == k_empty_tile)
    return nullptr;
  // tilesets are sorted ascending by first_gid (loader-guaranteed), so the owner is the last
  // entry whose first_gid is <= gid.
  const auto it = std::ranges::upper_bound(tilesets, gid, std::ranges::less{}, &TilemapTileset::first_gid);
  if (it == tilesets.begin())
    return nullptr;
  const TilemapTileset &result = *(it - 1);
  const int local_id = static_cast<int>(gid) - static_cast<int>(result.first_gid);
  if (local_id >= result.info.tile_count)
    return nullptr;
  return &result;
}

/** A tile's anchor point, as a fraction of its *full* (untrimmed) frame — horizontal measured from
 *  the left edge (0 = left), vertical from the bottom edge (0 = bottom). Sourced from
 *  TilesetInfo::tile_pivot_x/y, which the loader converts from spritepacker's trimmed-box, raster
 *  (top=0) convention into this full-frame, bottom-origin one.
 */
struct TilePivot {
  float x = 0.5f;

  float y = 0.f;
};

/** Returns the pivot for `local_id` in `info`; {0.5, 0} if local_id is out of range. */
[[nodiscard]] inline TilePivot get_tile_pivot(const TilesetInfo &info, int local_id) noexcept {
  const auto i = static_cast<std::size_t>(local_id);
  if (local_id < 0 || i >= info.tile_pivot_x.size() || i >= info.tile_pivot_y.size())
    return {};
  return {.x = info.tile_pivot_x[i], .y = info.tile_pivot_y[i]};
}

/** A tile's placement data relative to its own full (untrimmed) frame: how far the sampled,
 *  already-trimmed tile_rects entry is offset from that frame's top-left corner (trim_x/trim_y),
 *  and the frame's own size (full_width/full_height) — needed to scale TilePivot into pixels,
 *  since the trimmed rect alone can't recover the original canvas size.
 */
struct TileFrameOffset {
  int trim_x = 0;

  int trim_y = 0;

  int full_width = 0;

  int full_height = 0;
};

/** Returns the frame offset/size for `local_id` in `info`; all-zero if local_id is out of range. */
[[nodiscard]] inline TileFrameOffset get_tile_frame_offset(const TilesetInfo &info, int local_id) noexcept {
  const auto i = static_cast<std::size_t>(local_id);
  if (local_id < 0 || i >= info.tile_trim_x.size() || i >= info.tile_trim_y.size() ||
      i >= info.tile_full_width.size() || i >= info.tile_full_height.size())
    return {};
  return {
      .trim_x = info.tile_trim_x[i],
      .trim_y = info.tile_trim_y[i],
      .full_width = info.tile_full_width[i],
      .full_height = info.tile_full_height[i],
  };
}

/** Returns the source rect in the tileset texture for `gid` — spritepacker already trims and
 *  packs tightly, so this is a direct array read, not a computed grid cell.
 *  @param tileset Tileset entry that owns gid (caller must use find_tileset first).
 *  @param gid Global tile ID.
 *  @return Source rectangle, in pixels, within the tileset texture for this tile; all-zero if gid
 *          resolves to a local_id outside the tileset's range.
 */
[[nodiscard]] inline corundum::core::math::IntRect tile_source_rect(const TilemapTileset &tileset,
                                                                    TileId gid) noexcept {
  const int local_id = static_cast<int>(gid) - static_cast<int>(tileset.first_gid);
  if (local_id < 0 || static_cast<std::size_t>(local_id) >= tileset.info.tile_rects.size())
    return {};
  return tileset.info.tile_rects[static_cast<std::size_t>(local_id)];
}

/** Axis-aligned rectangle in tile-grid space.
 *  Used as a temporary during loading; runtime collision uses CollisionRects (SoA).
 */
struct CollisionRect {
  float col = 0.f;

  float row = 0.f;

  float col_span = 0.f;

  float row_span = 0.f;
};

/** Non-owning Structure-of-Arrays view over collision rect data in tile-grid space.
 *
 *  Passed to collision functions. Enables SIMD auto-vectorization by
 *  exposing each field as a contiguous span rather than interleaved structs.
 *  Construct from CollisionRects::view() or directly from stack arrays.
 */
struct CollisionRectsView {
  /** Left edges (tile columns). */
  std::span<const float> cols;

  /** Top edges (tile rows). */
  std::span<const float> rows;

  /** Horizontal extents in tiles. */
  std::span<const float> col_spans;

  /** Vertical extents in tiles. */
  std::span<const float> row_spans;

  /** Per-rect elevation [0–255], same semantics as TilemapLayer::elevation; empty means
   *  "no elevation data for any rect in this view" and disables elevation filtering entirely.
   */
  std::span<const uint8_t> elevations;

  /** Number of rects in the view. */
  [[nodiscard]] std::size_t size() const noexcept { return cols.size(); }
};

namespace detail {
/** O(1) swap-and-pop erase of `i` across parallel SoA vectors; order is not preserved.
 *  @pre every vector is non-empty, shares the same size, and i < size().
 */
template <typename First, typename... Rest> void swap_pop_at(std::size_t i, First &first, Rest &...rest) noexcept {
  const std::size_t last = first.size() - 1;
  const auto apply = [i, last](auto &vector) {
    if (i != last)
      vector[i] = vector[last];
    vector.pop_back();
  };
  apply(first);
  (apply(rest), ...);
}
} // namespace detail

/** Owned Structure-of-Arrays storage for collision rects in tile-grid space.
 *
 *  Stores rect fields in four parallel vectors so that overlaps_any() can
 *  iterate each field as a contiguous float array, enabling auto-vectorization.
 *  Populated once at load time; no per-frame allocation.
 *
 *  CollisionRectsView is the non-owning counterpart passed to collision functions.
 */
struct CollisionRects {
  /** Left edges (tile columns). */
  std::vector<float> cols;

  /** Top edges (tile rows). */
  std::vector<float> rows;

  /** Horizontal extents in tiles. */
  std::vector<float> col_spans;

  /** Vertical extents in tiles. */
  std::vector<float> row_spans;

  /** Per-rect elevation [0–255]; see CollisionRectsView. */
  std::vector<uint8_t> elevations;

  /** Append one collision rect in tile-grid space. */
  // elevation (uint8_t) sits next to float row_span; both are required so the append keeps the
  // SoA layout, and the two types are only convertible by implicit narrowing which callers avoid.
  void push_back(float col, float row, float col_span, float row_span, uint8_t elevation = 0) {
    cols.push_back(col);
    rows.push_back(row);
    col_spans.push_back(col_span);
    row_spans.push_back(row_span);
    elevations.push_back(elevation);
  }

  /** Number of stored rects. */
  [[nodiscard]] std::size_t size() const noexcept { return cols.size(); }

  /** Non-owning view over the stored data. */
  [[nodiscard]] CollisionRectsView view() const noexcept {
    return {.cols = cols, .rows = rows, .col_spans = col_spans, .row_spans = row_spans, .elevations = elevations};
  }

  /** Remove the rect at index `i` in O(1) via swap-and-pop. Order is not preserved.
   *  @pre i < size()
   */
  void erase(std::size_t i) noexcept { detail::swap_pop_at(i, cols, rows, col_spans, row_spans, elevations); }
};

/** Identifies which corner of a tile is the empty (non-solid) region.
 *
 *  The solid region is the triangle that does NOT contain the named corner.
 *  NW/SE triangles share the TR→BL hypotenuse; NE/SW share the TL→BR hypotenuse.
 */
enum class TriangleCut : uint8_t { NorthWest, NorthEast, SouthWest, SouthEast };

/** Non-owning Structure-of-Arrays view over diagonal collision triangle data in tile-grid space. */
struct CollisionTrianglesView {
  /** Left edges (tile columns). */
  std::span<const float> cols;

  /** Top edges (tile rows). */
  std::span<const float> rows;

  /** Horizontal extents in tiles. */
  std::span<const float> col_spans;

  /** Vertical extents in tiles. */
  std::span<const float> row_spans;

  /** Which corner is empty. */
  std::span<const TriangleCut> cuts;

  /** Per-triangle elevation [0–255]; empty disables elevation filtering. See CollisionRectsView. */
  std::span<const uint8_t> elevations;

  /** Number of triangles in the view. */
  [[nodiscard]] std::size_t size() const noexcept { return cols.size(); }
};

/** Owned Structure-of-Arrays storage for diagonal half-tile collision shapes in tile-grid space.
 *
 *  Each entry is a right-isoceles triangle occupying half a tile. The empty corner
 *  is identified by TriangleCut; the other half is solid.
 */
struct CollisionTriangles {
  /** Left edges (tile columns). */
  std::vector<float> cols;

  /** Top edges (tile rows). */
  std::vector<float> rows;

  /** Horizontal extents in tiles. */
  std::vector<float> col_spans;

  /** Vertical extents in tiles. */
  std::vector<float> row_spans;

  /** Which corner is empty. */
  std::vector<TriangleCut> cuts;

  /** Per-triangle elevation [0–255]; see CollisionTrianglesView. */
  std::vector<uint8_t> elevations;

  /** Append one diagonal collision triangle in tile-grid space. */
  void push_back(float col, float row, float col_span, float row_span, TriangleCut cut, uint8_t elevation = 0) {
    cols.push_back(col);
    rows.push_back(row);
    col_spans.push_back(col_span);
    row_spans.push_back(row_span);
    cuts.push_back(cut);
    elevations.push_back(elevation);
  }

  /** Number of stored triangles. */
  [[nodiscard]] std::size_t size() const noexcept { return cols.size(); }

  /** Non-owning view over the stored data. */
  [[nodiscard]] CollisionTrianglesView view() const noexcept {
    return {
        .cols = cols,
        .rows = rows,
        .col_spans = col_spans,
        .row_spans = row_spans,
        .cuts = cuts,
        .elevations = elevations,
    };
  }

  /** Remove the triangle at index `i` in O(1) via swap-and-pop. Order is not preserved.
   *  @pre i < size()
   */
  void erase(std::size_t i) noexcept { detail::swap_pop_at(i, cols, rows, col_spans, row_spans, cuts, elevations); }
};

/** Static multi-layer tile grid loaded from a JSON map file. */
struct Tilemap {
  /** Source file path passed to load_tilemap(). */
  std::string path;

  /** Sorted ascending by first_gid. */
  std::vector<TilemapTileset> tilesets;

  int width = 0;

  int height = 0;

  /** Isometric world step width in pixels (diamond width). 0 = use frame_width. */
  int iso_diamond_w = 0;

  /** Isometric world step height in pixels (diamond height). 0 = iso_diamond_w / 2. */
  int iso_diamond_h = 0;

  /** Index 0 is the bottom-most layer. */
  std::vector<TilemapLayer> layers;

  /** Impassable rects in tile-grid space; empty if absent. */
  CollisionRects collisions;

  /** Diagonal half-tile collision shapes; empty if absent. */
  CollisionTriangles collision_triangles;

  int max_tile_full_w = 0;

  int max_tile_full_h = 0;

  /** Effective isometric diamond width (world step), falling back to the first tileset's first
   *  tile's full frame width if not set.
   */
  [[nodiscard]] int diamond_w() const noexcept {
    if (iso_diamond_w > 0)
      return iso_diamond_w;
    if (tilesets.empty() || tilesets[0].info.tile_full_width.empty())
      return 0;
    return tilesets[0].info.tile_full_width[0];
  }

  /** Effective isometric diamond height, falling back to diamond_w / 2 if not set. */
  [[nodiscard]] int diamond_h() const noexcept {
    if (iso_diamond_h > 0)
      return iso_diamond_h;
    return diamond_w() / 2;
  }

  /** Tile GID at (col, row) in `layer`; undefined behaviour if out of bounds.
   *  @pre `layer` belongs to this Tilemap.
   */
  [[nodiscard]] TileId tile_at(const TilemapLayer &layer, int col, int row) const noexcept {
    return layer.tile_at_unchecked(col, row, width);
  }

  /** 2D mdspan view over `layer`'s tile grid — rows × cols layout. Lives on Tilemap
   *  (not TilemapLayer) since width/height are Tilemap-owned; layer doesn't self-construct.
   *  @pre &layer must belong to this Tilemap's layers, and layer.tiles.size() == width * height.
   */
  [[nodiscard]] auto layer_view(TilemapLayer &layer) const noexcept {
    assert(layer.tiles.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) &&
           "layer_view: layer.tiles must be width * height");
    return std::mdspan<TileId, std::dextents<std::size_t, 2>>(layer.tiles.data(), static_cast<std::size_t>(height),
                                                              static_cast<std::size_t>(width));
  }

  /** Const overload of layer_view(). */
  [[nodiscard]] auto layer_view(const TilemapLayer &layer) const noexcept {
    assert(layer.tiles.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) &&
           "layer_view: layer.tiles must be width * height");
    return std::mdspan<const TileId, std::dextents<std::size_t, 2>>(
        layer.tiles.data(), static_cast<std::size_t>(height), static_cast<std::size_t>(width));
  }
};

/** Check whether (col, row) is a valid tile index for `tilemap`.
 *
 *  @return true if col and row are both non-negative and less than the map's
 *          width/height respectively.
 */
[[nodiscard]] inline bool in_bounds(const Tilemap &tilemap, int col, int row) noexcept {
  return col >= 0 && row >= 0 && col < tilemap.width && row < tilemap.height;
}

template <std::predicate<std::size_t> KeepLayer>
[[nodiscard]] int elevation_at(const Tilemap &tilemap, int col, int row, const KeepLayer &keep_layer) noexcept;

/** Elevation of the tile at (col, row), for entities standing on it.
 *
 *  A cell can carry independent elevation data on more than one z_index==0
 *  layer; this resolves to the last (topmost-painted) such layer that has a
 *  tile present at the cell — a deliberate convention, since no single
 *  canonical "ground layer" is otherwise designated.
 *
 *  @return Elevation [0–255], or 0 if out of bounds (use in_bounds() to
 *          disambiguate a legitimate zero elevation from the OOB sentinel),
 *          no z_index==0 layer has a tile there, or the layer has no
 *          elevation data.
 *
 *  @note Out-of-bounds → 0 is the intentional sentinel: the downstream
 *        WalkabilityGraph allows movement at map edges (can_move returns
 *        true for OOB cells), and interpolated_elevation_at relies on the 0
 *        fallback when sampling outside the map during interpolation.
 */
[[nodiscard]] inline int elevation_at(const Tilemap &tilemap, int col, int row) noexcept {
  return elevation_at(tilemap, col, row, [](std::size_t /*layer_index*/) { return true; });
}

/** elevation_at(), counting only the layers `keep_layer` accepts (by index in tilemap.layers).
 *  Same resolution rule: the topmost accepted z_index==0 layer with a tile at the cell wins.
 */
template <std::predicate<std::size_t> KeepLayer>
[[nodiscard]] int elevation_at(const Tilemap &tilemap, int col, int row, const KeepLayer &keep_layer) noexcept {
  if (!in_bounds(tilemap, col, row))
    return 0;
  const std::size_t uidx = cell_index(tilemap.width, col, row);
  if (uidx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return 0;
  const int idx = static_cast<int>(uidx);
  int result = 0;
  for (std::size_t layer_index = 0; layer_index < tilemap.layers.size(); ++layer_index) {
    const TilemapLayer &layer = tilemap.layers[layer_index];
    if (layer.z_index != 0 || !keep_layer(layer_index))
      continue;
    if (uidx >= layer.elevation.size())
      continue;
    const bool has_tile =
        layer.animated_cells.contains(idx) || (uidx < layer.tiles.size() && layer.tiles[uidx] != k_empty_tile);
    if (!has_tile)
      continue;
    result = layer.elevation[uidx];
  }
  return result;
}

/** Ramp axis of the tile at (col, row), if it's a ramp.
 *
 *  Same topmost-z_index==0-layer-with-a-tile resolution convention as elevation_at().
 *
 *  @param tilemap The tilemap to query.
 *  @param col Column index (0-based).
 *  @param row Row index (0-based).
 *  @return The axis this cell bridges, or std::nullopt if out of bounds, no z_index==0 layer
 *          has a tile there, or the cell isn't a ramp.
 */
[[nodiscard]] inline std::optional<RampAxis> ramp_axis_at(const Tilemap &tilemap, int col, int row) noexcept {
  if (!in_bounds(tilemap, col, row))
    return std::nullopt;
  const std::size_t uidx = cell_index(tilemap.width, col, row);
  if (uidx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return std::nullopt;
  const int idx = static_cast<int>(uidx);
  std::optional<RampAxis> result;
  for (const auto &layer : tilemap.layers) {
    if (layer.z_index != 0)
      continue;
    const bool has_tile =
        layer.animated_cells.contains(idx) || (uidx < layer.tiles.size() && layer.tiles[uidx] != k_empty_tile);
    if (!has_tile)
      continue;
    if (const auto it = layer.ramps.find(idx); it != layer.ramps.end())
      result = it->second;
    else
      result = std::nullopt;
  }
  return result;
}

/** Smoothly-interpolated elevation at a fractional world position — used for the
 *  entity height lift so crossing a ramp doesn't pop between its two flanking elevations.
 *
 *  Falls back to plain elevation_at() (floored) for any non-ramp cell, so behavior for the
 *  rest of the map is unchanged. Single-map mode only — chunked/streamed World mode has no
 *  caller for this yet and keeps the discrete per-tile elevation_at() lift.
 *
 *  @param tilemap The tilemap to query.
 *  @param col_f   Fractional column (e.g. an entity's world position in tile-grid units).
 *  @param row_f   Fractional row.
 *  @return Elevation, linearly interpolated across a ramp cell along its axis.
 */
[[nodiscard]] inline float interpolated_elevation_at(const Tilemap &tilemap, float col_f, float row_f) noexcept {
  const int col = static_cast<int>(std::floor(col_f));
  const int row = static_cast<int>(std::floor(row_f));
  const std::optional<RampAxis> axis = ramp_axis_at(tilemap, col, row);
  if (!axis)
    return static_cast<float>(elevation_at(tilemap, col, row));
  if (*axis == RampAxis::NorthSouth) {
    const float t = row_f - static_cast<float>(row); // 0 at the north edge, 1 at the south edge
    return std::lerp(static_cast<float>(elevation_at(tilemap, col, row - 1)),
                     static_cast<float>(elevation_at(tilemap, col, row + 1)), t);
  }
  const float t = col_f - static_cast<float>(col); // 0 at the west edge, 1 at the east edge
  return std::lerp(static_cast<float>(elevation_at(tilemap, col - 1, row)),
                   static_cast<float>(elevation_at(tilemap, col + 1, row)), t);
}

/** Material tag for `layer` at cell `idx`, or std::nullopt when the layer has no
 *  z_index==0 tile there.
 *
 *  @param tilemap The tilemap owning `layer`, for the tileset default lookup.
 *  @param layer Layer to resolve.
 *  @param uidx Byte index of the cell in the layer's tile array.
 *  @param idx Signed cell index used by the override and animation maps.
 */
[[nodiscard]] inline std::optional<std::string> layer_material_at(const Tilemap &tilemap, const TilemapLayer &layer,
                                                                  std::size_t uidx, int idx) {
  if (layer.z_index != 0)
    return std::nullopt;
  if (uidx >= layer.tiles.size())
    return std::nullopt;

  const bool is_animated = layer.animated_cells.contains(idx);
  if (!is_animated && layer.tiles[uidx] == k_empty_tile)
    return std::nullopt;

  if (const auto it = layer.material_overrides.find(idx); it != layer.material_overrides.end())
    return it->second;

  TileId gid = layer.tiles[uidx];
  if (is_animated) {
    const auto &frames = layer.animated_cells.at(idx).frame_gids;
    gid = frames.empty() ? k_empty_tile : frames[0];
  }
  const TilemapTileset *tileset = find_tileset(tilemap.tilesets, gid);
  return tileset != nullptr ? tileset->info.material : std::string{};
}

/** Terrain-material tag of the tile at (col, row), for footstep/ambient audio selection.
 *
 *  Resolves a per-cell material_overrides entry on the topmost z_index==0 layer with a tile
 *  present at the cell (same layer-precedence convention as elevation_at); if that cell has no
 *  override, falls back to the owning tileset's default TilesetInfo::material. This keeps the
 *  common case (every tile of a terrain type sharing one material) free, while still letting an
 *  individual placed tile diverge from its tileset's default (e.g. a snow-dusted patch of an
 *  otherwise-stone floor) without deriving the tag from the tile's visual art index.
 *
 *  @param tilemap The tilemap to query.
 *  @param col Column index (0-based, left to right).
 *  @param row Row index (0-based, top to bottom).
 *  @return The resolved material tag, or an empty string if out of bounds (use in_bounds() to
 *          disambiguate a legitimate empty material from the OOB sentinel), no z_index==0 layer
 *          has a tile there, or neither an override nor a tileset default is set.
 *
 *  @note Out-of-bounds → empty string is the intentional sentinel — see elevation_at() for the
 *        rationale that applies equally to material resolution.
 */
[[nodiscard]] inline std::string material_at(const Tilemap &tilemap, int col, int row) {
  if (!in_bounds(tilemap, col, row))
    return {};
  const std::size_t uidx = cell_index(tilemap.width, col, row);
  if (uidx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return {};
  const int idx = static_cast<int>(uidx);

  std::string result;
  for (const auto &layer : tilemap.layers) {
    if (const std::optional<std::string> material = layer_material_at(tilemap, layer, uidx, idx); material)
      result = *material;
  }
  return result;
}

/** Validates a tilemap for author-time errors: orphaned tile references, duplicate layer
 *  names, layer tile-count mismatches, out-of-bounds collision geometry, and ramps whose
 *  axis-neighbors fall outside the map.
 *
 *  Intended to be run explicitly before save/export (e.g. by an editor), not as a load-time gate;
 *  load_tilemap() does the load-time structural checks.
 *
 *  @param tilemap The tilemap to validate.
 *  @return One human-readable message per problem found, each naming the offending layer/cell/rect
 *          so an editor can point the author at a specific location. Empty if the map is valid.
 *
 *  @note A non-positive width or height is reported as a single error and short-circuits the
 *        remaining checks, which all index cells by width.
 */
[[nodiscard]] std::vector<std::string> validate(const Tilemap &tilemap);

} // namespace corundum::world::tilemap
