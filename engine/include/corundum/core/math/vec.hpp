// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>

namespace corundum::core::math {

  /// 2D float vector for positions, sizes, and scales.
  struct Vec2 {
    float x = 0.f;
    float y = 0.f;
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

} // namespace corundum::core::math
