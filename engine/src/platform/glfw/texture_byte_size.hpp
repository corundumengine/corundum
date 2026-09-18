// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>

namespace corundum::platform::glfw {

  /** @brief Byte size of the RGBA8 buffer @p width x @p height describes.
   *
   * @pre @p width and @p height are non-negative.
   * @note The product is widened to `std::size_t` before multiplying, so an oversized dimension
   *       cannot overflow the way an `int` product would before the upload is validated.
   */
  [[nodiscard]] constexpr std::size_t rgba8_byte_count(int width, int height) noexcept {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  }

} // namespace corundum::platform::glfw
