#pragma once
#include <cstdint>

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
   * Pass this to tile_to_world / cart_to_iso to avoid computing half_tw, half_th,
   * and x_origin separately at every call site.
   */
  struct IsoParams {
    /** @brief Half the scaled diamond width (diamond_w * tile_scale * 0.5). */
    float half_tw{};
    /** @brief Half the scaled diamond height (diamond_h * tile_scale * 0.5). */
    float half_th{};
    /**
     * @brief Horizontal origin shift so the leftmost tile (0, height-1) lands at x = 0.
     *   Equals (height - 1) * half_tw.
     */
    float x_origin{};
  };

  /**
   * @brief Build IsoParams from raw map/metric values.
   *
   * @param diamond_w  Tilemap::diamond_w() – the isometric grid-step width  in Tiled pixels.
   * @param diamond_h  Tilemap::diamond_h() – the isometric grid-step height in Tiled pixels.
   * @param height     Number of tile rows in the map (Tilemap::height).
   * @param tile_scale Display scale factor (e.g. 1.f, 2.f).
   * @return IsoParams with half_tw, half_th, x_origin pre-computed.
   */
  [[nodiscard]] constexpr IsoParams compute_iso_params(int diamond_w, int diamond_h, int height,
                                                       float tile_scale) noexcept {
    const float half_tw = static_cast<float>(diamond_w) * tile_scale * 0.5f;
    const float half_th = static_cast<float>(diamond_h) * tile_scale * 0.5f;
    const float x_origin = static_cast<float>(height - 1) * half_tw;
    return {half_tw, half_th, x_origin};
  }

  /**
   * @brief Full diamond cell height in screen pixels (= 2 * half_th).
   *
   * This is the vertical distance from the top vertex to the southern (bottom)
   * vertex of one isometric diamond cell.  Anchor at this offset from
   * tile_to_world to place a sprite so its artwork fills the cell naturally.
   *
   * @param half_th  Half the scaled diamond height (from IsoParams or equivalent).
   * @return The full cell height in screen pixels.
   */
  [[nodiscard]] constexpr float diamond_cell_height(float half_th) noexcept {
    return half_th * 2.f;
  }

  /**
   * @brief Convert a tile grid position and elevation to an isometric screen offset.
   *
   * The origin (0, 0) is the top vertex of the diamond grid.
   * Increasing tx moves the tile right and down; increasing ty moves it left and down.
   * Increasing elevation lifts the tile upward on-screen.
   *
   * @param tx        Tile column (0-based).
   * @param ty        Tile row (0-based).
   * @param elevation Tile height [0–255]; 0 is flat ground level.
   * @param half_tw   Half the scaled diamond width in screen pixels  (e.g. 32 for a 64-px diamond).
   * @param half_th   Half the scaled diamond height in screen pixels (e.g. 16 for a 32-px diamond).
   * @param elev_step Screen pixels lifted per unit of elevation.
   * @return Isometric world-space position of the tile's anchor point.
   */
  [[nodiscard]] constexpr Vec2 tile_to_screen(int tx, int ty, int elevation, float half_tw, float half_th,
                                              float elev_step) noexcept {
    return {
        static_cast<float>(tx - ty) * half_tw,
        static_cast<float>(tx + ty) * half_th - static_cast<float>(elevation) * elev_step,
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
  [[nodiscard]] constexpr Vec2 tile_to_world(int tx, int ty, int elevation, float half_tw, float half_th,
                                             float elev_step, float x_origin) noexcept {
    return {
        static_cast<float>(tx - ty) * half_tw + x_origin,
        static_cast<float>(tx + ty) * half_th - static_cast<float>(elevation) * elev_step,
    };
  }

  /**
   * @brief Convert an isometric world-space position back to fractional tile-grid
   * coordinates, given an assumed elevation. The direct inverse of tile_to_world —
   * distinct from cart_to_iso/iso_to_cart, which invert the legacy Cartesian-pixel
   * convention (col*tile_w, row*tile_h) used at the physics/collision boundary and
   * take no elevation term.
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
  [[nodiscard]] constexpr Vec2 world_to_tile(Vec2 world_pos, int elevation, float half_tw, float half_th,
                                             float elev_step, float x_origin) noexcept {
    const float adj_x = world_pos.x - x_origin;
    const float adj_y = world_pos.y + static_cast<float>(elevation) * elev_step;
    const float u = adj_x / half_tw; // tx - ty
    const float v = adj_y / half_th; // tx + ty
    return {(u + v) * 0.5f, (v - u) * 0.5f};
  }

  /**
   * @brief Convert a Cartesian tile-pixel position to isometric world space.
   *
   * Cartesian coordinates are the legacy internal format:
   *   cart.x = col * tile_w,  cart.y = row * tile_h  (tile_w == tile_h).
   * Used at physics/collision boundaries where rects remain in Cartesian space.
   *
   * @param cart     Cartesian tile-pixel position.
   * @param half_tw  Half the scaled diamond width (= tile_w / 2 when diamond_w == tile_px).
   * @param half_th  Half the scaled diamond height (= tile_w / 4 for 2:1 isometric).
   * @param x_origin Horizontal shift from tile_to_world (pass 0 if not using origin shift).
   * @return Isometric world-space position.
   */
  [[nodiscard]] constexpr Vec2 cart_to_iso(Vec2 cart, float half_tw, float half_th, float x_origin = 0.f) noexcept {
    const float tile_w = half_tw * 2.f;
    return {
        (cart.x - cart.y) * half_tw / tile_w + x_origin,
        (cart.x + cart.y) * half_th / tile_w,
    };
  }

  /**
   * @brief Convert an isometric world-space position back to Cartesian tile-pixel space.
   *
   * Inverse of cart_to_iso. Used in the physics system to check entity positions
   * against Cartesian collision rects.
   *
   * @param iso      Isometric world-space position.
   * @param half_tw  Half the scaled diamond width.
   * @param half_th  Half the scaled diamond height.
   * @param x_origin The same x_origin passed to cart_to_iso (subtracted before inversion).
   * @return Cartesian tile-pixel position (col * tile_w, row * tile_h).
   */
  [[nodiscard]] constexpr Vec2 iso_to_cart(Vec2 iso, float half_tw, float half_th, float x_origin = 0.f) noexcept {
    const float tile_w = half_tw * 2.f;
    const float adj_x = iso.x - x_origin;
    const float col_f = (adj_x / half_tw + iso.y / half_th) * 0.5f;
    const float row_f = (iso.y / half_th - adj_x / half_tw) * 0.5f;
    return {col_f * tile_w, row_f * tile_w};
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
    return (tx + ty) + elevation * (half_th > 0.f ? elev_step / half_th : 0.f);
  }

} // namespace corundum::core::math
