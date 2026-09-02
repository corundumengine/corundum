#pragma once
#include <cstdint>
#include <limits>

namespace corundum::core::math {

  /// 2D float vector for positions, sizes, and scales.
  struct Vec2 {
    float x = 0.f;
    float y = 0.f;
  };

  /// 3D float vector.
  struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
  };

  /// 4D float vector.
  struct Vec4 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 0.f;
  };

  /// Unit quaternion for rotations.
  struct Quat {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float w = 1.f;
  };

  /// Axis-aligned integer rectangle for texture source regions.
  struct IntRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  /// RGBA colour with 8-bit channels.
  struct Colour {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    uint8_t a = 255;
  };

  /**
   * @brief Packed isometric projection parameters for a tilemap at a given scale.
   *
   * Pass this to tile_to_world / world_to_tile to avoid computing half_tw, half_th,
   * x_origin, and elev_step separately at every call site.
   */
  struct IsometricParams {
    /** @brief Half the scaled diamond width (diamond_w * tile_scale * 0.5). */
    float half_tw{};
    /** @brief Half the scaled diamond height (diamond_h * tile_scale * 0.5). */
    float half_th{};
    /**
     * @brief Horizontal origin shift so the leftmost tile (0, height-1) lands at x = 0.
     *   Equals (height - 1) * half_tw.
     */
    float x_origin{};
    /** @brief Screen pixels lifted per unit of elevation, already scaled by tile_scale. */
    float elev_step{};
  };

  /**
   * @brief Build IsometricParams from raw map/metric values.
   *
   * @param diamond_w  Tilemap::diamond_w() – the isometric grid-step width  in Tiled pixels.
   * @param diamond_h  Tilemap::diamond_h() – the isometric grid-step height in Tiled pixels.
   * @param height     Number of tile rows in the map (Tilemap::height).
   * @param tile_scale Display scale factor (e.g. 1.f, 2.f).
   * @param elev_step  Raw screen pixels lifted per unit of elevation
   *                   (GameConfig::elevation_step_px) — multiplied by tile_scale
   *                   just like half_tw/half_th so elevation lift scales with zoom.
   * @return IsometricParams with half_tw, half_th, x_origin, elev_step pre-computed
   *         (all scaled by tile_scale).
   */
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // Raw scalar overloads are the low-level projection kernels exercised directly by
  // test_iso_math.cpp; production callers should prefer the IsometricParams overloads
  // below (which bundle these fields into one struct and eliminate the swap hazard).
  [[nodiscard]] constexpr IsometricParams compute_isometric_params(int diamond_w, int diamond_h, int height,
                                                                   float tile_scale, float elev_step) noexcept {
    const float half_tw = static_cast<float>(diamond_w) * tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h) * tile_scale * 0.5f;
    const float x_origin = static_cast<float>(height - 1) * half_tw;
    return {.half_tw = half_tw, .half_th = half_th, .x_origin = x_origin, .elev_step = elev_step * tile_scale};
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

  /**
   * @brief Full diamond cell height in screen pixels (= 2 * half_th).
   *
   * The vertical distance from the top vertex to the southern (bottom) vertex
   * of one isometric diamond cell — i.e. the corner-to-corner span of the
   * diamond.  Useful for outlines and shape math (e.g. tilesmith's hover
   * diamond outline traces its four corners relative to an internal anchor
   * using this span).
   *
   * @note Do not use this as a sprite-anchor offset from tile_to_world():
   * that anchors the sprite at the cell's southern vertex, which renders the
   * sprite shifted one half-diamond below the cell center under this
   * codebase's TOP-vertex projection convention.  For sprite anchors, use
   * `half_th` directly (cell center) — the same convention already relied on
   * by physics and picking (see tests/test_picking.cpp).
   *
   * @param half_th  Half the scaled diamond height (from IsometricParams or equivalent).
   * @return The full cell height in screen pixels.
   */
  [[nodiscard]] constexpr float diamond_cell_height(float half_th) noexcept {
    return half_th * 2.f;
  }

  /**
   * @brief Vertical offset from the top of a (full, untrimmed) frame to a
   * bottom-origin pivot, in scaled pixels.
   *
   * Converts a bottom-origin pivot.y (0 = frame bottom, 1 = frame top — see
   * TilePivot in tilemap.hpp) to a top-origin offset suitable for sprite
   * placement: how far down from `position.y` the pivot lands.
   *
   * @param pivot_y      Bottom-origin pivot coordinate in [0, 1].
   * @param frame_height Full frame height in scaled pixels.
   * @return Offset in scaled pixels from the top of the frame to the pivot.
   */
  [[nodiscard]] constexpr float pivot_top_offset(float pivot_y, float frame_height) noexcept {
    return (1.f - pivot_y) * frame_height;
  }

  /**
   * @brief Convert a tile grid position and elevation to an isometric screen offset.
   *
   * Returns the projection point used by this codebase as the cell's top (north) vertex.
   * Increasing tx moves the tile right and down; increasing ty moves it left and down.
   * Increasing elevation lifts the tile upward on-screen.
   *
   * Under the codebase's TOP-vertex projection convention, the cell extends downward from
   * this point to (this point + (0, 2*half_th)) — i.e. the southern vertex — and the
   * cell's geometric center is at (this point + (0, half_th)). The grid renderer draws
   * lines through top vertices of cells, which in this projection outline the cells
   * themselves (the four top vertices of a 2x2 block form exactly the diamond of the
   * top-left cell).
   *
   * @param tx        Tile column (0-based).
   * @param ty        Tile row (0-based).
   * @param elevation Tile height [0–255]; 0 is flat ground level.
   * @param half_tw   Half the scaled diamond width in screen pixels  (e.g. 32 for a 64-px diamond).
   * @param half_th   Half the scaled diamond height in screen pixels (e.g. 16 for a 32-px diamond).
   * @param elev_step Screen pixels lifted per unit of elevation.
   * @return Isometric world-space projection point for the cell at (tx, ty).
   */
  [[nodiscard]] constexpr Vec2 tile_to_screen(int tx, int ty, int elevation, float half_tw, float half_th,
                                              float elev_step) noexcept {
    return {
        .x = static_cast<float>(tx - ty) * half_tw,
        .y = (static_cast<float>(tx + ty) * half_th) - (static_cast<float>(elevation) * elev_step),
    };
  }

  /**
   * @brief tile_to_screen with an x-origin shift so the leftmost tile lands at x = 0.
   *
   * @p x_origin should be `(map_height_in_tiles - 1) * half_tw`. This matches the
   * editor's x_shift convention and ensures camera.x is always non-negative.
   *
   * @param tx, ty, elevation, half_tw, half_th, elev_step  Same as tile_to_screen.
   * @param x_origin  Horizontal shift applied to the result.
   * @return Isometric world-space position offset by x_origin.
   */
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  [[nodiscard]] constexpr Vec2 tile_to_world(int tx, int ty, int elevation, float half_tw, float half_th,
                                             float elev_step, float x_origin) noexcept {
    return {
        .x = (static_cast<float>(tx - ty) * half_tw) + x_origin,
        .y = (static_cast<float>(tx + ty) * half_th) - (static_cast<float>(elevation) * elev_step),
    };
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

  /**
   * @brief Convert an isometric world-space position back to fractional tile-grid
   * coordinates, given an assumed elevation. The direct inverse of tile_to_world.
   *
   * @param world_pos Isometric world-space position (already camera-adjusted).
   * @param elevation Assumed tile height — the caller supplies this per-candidate
   *                  when resolving which of several stacked tiles a screen point hits.
   * @param half_tw   Half the scaled diamond width.
   * @param half_th   Half the scaled diamond height.
   * @param elev_step Screen pixels lifted per unit of elevation.
   * @param x_origin  The same x_origin passed to tile_to_world.
   * @return Fractional {col, row}; floor() each component to get the containing cell.
   */
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  [[nodiscard]] constexpr Vec2 world_to_tile(Vec2 world_pos, int elevation, float half_tw, float half_th,
                                             float elev_step, float x_origin) noexcept {
    const float adj_x = world_pos.x - x_origin;
    const float adj_y = world_pos.y + (static_cast<float>(elevation) * elev_step);
    const float u = adj_x / half_tw; // tx - ty
    const float v = adj_y / half_th; // tx + ty
    return {.x = (u + v) * 0.5f, .y = (v - u) * 0.5f};
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

  /**
   * @brief Convert a fractional tile grid position and elevation to an isometric
   * world-space position using a packed IsometricParams struct.
   *
   * Overload for callers that already hold an IsometricParams. Fractional col/row lets
   * camera-centering and interpolated entity positions call this directly without
   * re-inlining the projection.
   *
   * @param col       Tile column (fractional).
   * @param row       Tile row (fractional).
   * @param elevation Tile height [0–255]; 0 is flat ground level.
   * @param iso       Packed projection parameters from compute_isometric_params().
   * @return Isometric world-space position.
   */
  [[nodiscard]] constexpr Vec2 tile_to_world(float col, float row, int elevation, const IsometricParams &iso) noexcept {
    return {
        .x = ((col - row) * iso.half_tw) + iso.x_origin,
        .y = ((col + row) * iso.half_th) - (static_cast<float>(elevation) * iso.elev_step),
    };
  }

  /**
   * @brief Convert an isometric world-space position back to fractional tile-grid
   * coordinates using a packed IsometricParams struct. Overload for callers that already
   * hold an IsometricParams.
   *
   * @param world_pos Isometric world-space position (already camera-adjusted).
   * @param elevation Assumed tile height.
   * @param iso       Packed projection parameters from compute_isometric_params().
   * @return Fractional {col, row}; floor() each component to get the containing cell.
   */
  [[nodiscard]] constexpr Vec2 world_to_tile(Vec2 world_pos, int elevation, const IsometricParams &iso) noexcept {
    return world_to_tile(world_pos, elevation, iso.half_tw, iso.half_th, iso.elev_step, iso.x_origin);
  }

  /**
   * @brief Cell-center anchor convention: returns the cell's geometric center in
   * isometric world space, NOT its top vertex.
   *
   * Under the codebase's TOP-vertex projection convention (tile_to_world returns
   * the cell's top (north) vertex), every consumer that places an entity sprite,
   * camera target, or feet marker needs the cell's center — `top_vertex + (0,
   * half_th)` — to align with where the tile sprite draws. Centralizing this here
   * prevents the anchor drift that left actors floating half_th above the ground
   * or sunk below it.
   *
   * Uses @p float elevation (rather than int) to preserve interpolated elevation
   * precision on ramps, matching what `tile_to_world`'s existing callers do
   * inline at entity/camera sites.
   *
   * @param col        Tile column (fractional).
   * @param row        Tile row (fractional).
   * @param elevation  Tile height (may be fractional for interpolated ramps).
   * @param iso        Packed projection parameters from compute_isometric_params().
   * @return Isometric world-space position of the cell's geometric center.
   */
  [[nodiscard]] constexpr Vec2 tile_to_world_center(float col, float row, float elevation,
                                                    const IsometricParams &iso) noexcept {
    return {
        .x = ((col - row) * iso.half_tw) + iso.x_origin,
        .y = ((col + row) * iso.half_th) - (elevation * iso.elev_step) + iso.half_th,
    };
  }

  /**
   * @brief Convert a tile-grid delta (dc, dr) to the equivalent screen-space delta.
   *
   * Applies the isometric projection's Jacobian to a delta vector (no x_origin shift —
   * this is for deltas/velocities, not positions), so that normalising in screen space
   * produces equal perceived speed in all directions rather than equal tile-grid
   * distance, which the isometric projection distorts unevenly by direction.
   *
   * @param dc, dr  Tile-grid delta (e.g. a velocity in tiles/s, or a displacement).
   * @param iso     Packed projection parameters from compute_isometric_params().
   * @return Screen-space delta {dx, dy}.
   */
  [[nodiscard]] constexpr Vec2 tile_to_screen_delta(float dc, float dr, const IsometricParams &iso) noexcept {
    return {.x = (dc - dr) * iso.half_tw, .y = (dc + dr) * iso.half_th};
  }

  /**
   * @brief Draw-order depth key that accounts for elevation, for painter's-algorithm sorting.
   *
   * Extends the plain grid depth (tx + ty) with an elevation term scaled so that an
   * elevation delta which lifts a tile by exactly one grid-step's worth of screen
   * pixels (half_th) shifts the depth by exactly 1.0 — the same units as a genuine
   * one-cell grid move. This keeps the key consistent with tile_to_screen/tile_to_world's
   * geometry instead of using an arbitrary weight, so a raised tile correctly sorts
   * after (draws on top of/occludes) a lower neighboring tile once its screen-space
   * lift visually overhangs into that neighbor's footprint.
   *
   * @param tx, ty    Tile column/row (fractional for interpolated entity positions).
   * @param elevation Tile height, same units as tile_to_screen's elevation parameter.
   * @param half_th   Half the scaled diamond height in screen pixels.
   * @param elev_step Screen pixels lifted per unit of elevation.
   * @return Depth key; smaller draws first (further back).
   */
  [[nodiscard]] constexpr float iso_depth_key(float tx, float ty, float elevation, float half_th,
                                              float elev_step) noexcept {
    return (tx + ty) + (elevation * (half_th > 0.f ? elev_step / half_th : 0.f));
  }

  /**
   * @brief Draw-order depth key using packed IsometricParams.
   *
   * Overload for callers that already hold an IsometricParams.
   */
  [[nodiscard]] constexpr float iso_depth_key(float tx, float ty, float elevation,
                                              const IsometricParams &iso) noexcept {
    return iso_depth_key(tx, ty, elevation, iso.half_th, iso.elev_step);
  }

  // ── Viewport culling ───────────────────────────────────────────────────

  /**
   * @brief Bounds of the visible isometric tile range for a world-space rectangle.
   *
   * Computed by compute_isometric_cull_bounds() from the camera's visible area
   * expanded by conservative pads. The depth range (min/max col+row) and u range
   * (min/max col−row) are used to clamp tile iteration loops to only visible bands.
   */
  struct IsometricCullBounds {
    int depth_min; ///< Smallest visible col+row (top-most band), inclusive.
    int depth_max; ///< Largest visible col+row (bottom-most band), inclusive.
    float u_min;   ///< Smallest visible (col − row), fractional.
    float u_max;   ///< Largest visible (col − row), fractional.
  };

  /**
   * @brief Compute culling bounds from a world-space rectangle.
   *
   * Converts a rect (left, top, right, bottom) in isometric world pixels
   * into tile-grid depth/col ranges that bracket every tile whose world
   * position falls inside the rect.
   *
   * @param left, top, right, bottom World-space rectangle (typically the camera
   *                                 viewport expanded by per-side art extent pads).
   * @param iso IsometricParams for the map (half_th, half_th, x_origin, elev_step).
   * @return Bounds covering every tile cell overlapping the rectangle.
   */
  [[nodiscard]] constexpr IsometricCullBounds
  compute_isometric_cull_bounds(float left, float top, float right, float bottom, const IsometricParams &iso) noexcept;

  /**
   * @brief First visible column in the given depth band for the supplied cull bounds.
   *
   * @param b Cull bounds from compute_isometric_cull_bounds().
   * @param depth Absolute depth (col + row) of the band.
   * @return Inclusive minimum column index; may be negative for off-screen bands.
   */
  [[nodiscard]] constexpr int isometric_cull_first_column(const IsometricCullBounds &b, int depth) noexcept;

  /**
   * @brief Last visible column in the given depth band for the supplied cull bounds.
   *
   * @param b Cull bounds from compute_isometric_cull_bounds().
   * @param depth Absolute depth (col + row) of the band.
   * @return Inclusive maximum column index.
   */
  [[nodiscard]] constexpr int isometric_cull_last_column(const IsometricCullBounds &b, int depth) noexcept;

  // ── Cull implementation (constexpr, no <cmath> for AppleClang compat) ────

  namespace detail {
    [[nodiscard]] constexpr float ce_floor(float x) noexcept {
      const int i = static_cast<int>(x);
      return x < static_cast<float>(i) ? static_cast<float>(i - 1) : static_cast<float>(i);
    }

    [[nodiscard]] constexpr float ce_ceil(float x) noexcept {
      const int i = static_cast<int>(x);
      return x > static_cast<float>(i) ? static_cast<float>(i + 1) : static_cast<float>(i);
    }
  } // namespace detail

  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  [[nodiscard]] constexpr IsometricCullBounds
  compute_isometric_cull_bounds(float left, float top, float right, float bottom, const IsometricParams &iso) noexcept {
    if (iso.half_tw <= 0.f || iso.half_th <= 0.f) {
      return {.depth_min = std::numeric_limits<int>::min() / 2,
              .depth_max = std::numeric_limits<int>::max() / 2,
              .u_min = -1e30f,
              .u_max = 1e30f};
    }
    const float inv_tw = 1.f / iso.half_tw;
    const float inv_th = 1.f / iso.half_th;
    const float u0 = (left - iso.x_origin) * inv_tw;
    const float u1 = (right - iso.x_origin) * inv_tw;
    const float v0 = top * inv_th;
    const float v1 = bottom * inv_th;
    return {
        .depth_min = static_cast<int>(detail::ce_floor(v0)),
        .depth_max = static_cast<int>(detail::ce_ceil(v1)),
        .u_min = detail::ce_floor(u0),
        .u_max = detail::ce_ceil(u1),
    };
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

  [[nodiscard]] constexpr int isometric_cull_first_column(const IsometricCullBounds &b, int depth) noexcept {
    const float u = b.u_min;
    const float df = static_cast<float>(depth);
    return static_cast<int>(detail::ce_ceil((u + df) * 0.5f));
  }

  [[nodiscard]] constexpr int isometric_cull_last_column(const IsometricCullBounds &b, int depth) noexcept {
    const float u = b.u_max;
    const float df = static_cast<float>(depth);
    return static_cast<int>(detail::ce_floor((u + df) * 0.5f));
  }

} // namespace corundum::core::math
