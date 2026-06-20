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

} // namespace corundum::core::math
