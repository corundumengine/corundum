// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace corundum::world {

  /**
   * @brief Isometric world dimensions in display pixels.
   *
   * Passive value: the two axes travel together so a caller cannot pass the width
   * where the height is expected. For non-square diamonds the two axes differ.
   */
  struct WorldBounds {
    float width_px{0.f}; ///< Total world width in display pixels.

    float height_px{0.f}; ///< Total world height in display pixels.
  };

} // namespace corundum::world
