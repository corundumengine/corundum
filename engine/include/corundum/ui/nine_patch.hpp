// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/platform/renderer.hpp>
#include <cstdint>

namespace corundum::ui {

  /// A nine-patch border defined by a uniform 3×3 grid of equal cells.
  ///
  /// The source texture is laid out as three equal columns and three equal rows of
  /// `tile_w × tile_h` cells, anchored at the texture origin. Corners are drawn at
  /// their natural cell size; edges are stretched to span the gap between them; the
  /// middle cell is never drawn.
  ///
  /// @note nine_patch_render() is a no-op when @c texture_id is 0 (no texture loaded)
  ///       or @c tile_w / @c tile_h are non-positive (malformed asset), so callers need
  ///       not guard those cases.
  struct NinePatchBorder {
    uint32_t texture_id{0};

    int tile_w{0}; ///< Width in pixels of one source cell; must be > 0 for the frame to draw.

    int tile_h{0}; ///< Height in pixels of one source cell; must be > 0 for the frame to draw.
  };

  /// Draw @p border as a frame around the rect at (@p x, @p y) with size (@p w, @p h).
  ///
  /// @param r      Renderer; receives up to eight DrawSprite commands (corners, then edges).
  /// @param border Border texture and cell dimensions.
  /// @param x,y    Top-left of the framed rect in screen pixels.
  /// @param w,h    Size of the framed rect in screen pixels; must be non-negative.
  /// @note A rect narrower or shorter than two cells collapses the affected inner span to
  ///       zero rather than emitting a negative-scaled (inverted) edge quad.
  void nine_patch_render(platform::Renderer &r, const NinePatchBorder &border, float x, float y, float w, float h);

} // namespace corundum::ui
