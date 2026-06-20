#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <cstdint>

namespace corundum::gameplay::ui {

  /// A nine-patch border defined by a uniform 3×3 tile grid.
  ///
  /// The source texture is assumed to be laid out as three equal columns and
  /// three equal rows of size tile_w × tile_h. Corners are drawn at their
  /// natural size; edges are stretched to fill the remaining span.
  ///
  /// @pre texture_id != 0 before calling render().
  struct NinePatchBorder {
    uint32_t texture_id{0};
    int tile_w{0}; ///< Width in pixels of one tile in the source texture.
    int tile_h{0}; ///< Height in pixels of one tile in the source texture.

    /// Draws the nine-patch frame around the rect at (x, y) with size (w, h).
    void render(platform::Renderer &r, float x, float y, float w, float h) const;
  };

} // namespace corundum::gameplay::ui
