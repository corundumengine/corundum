// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace corundum::toolkit::widgets {

  /**
   * @brief Copies @p text into the fixed-size @p buffer for an ImGui char* widget.
   *
   * Always null-terminates, truncating @p text when it does not fit, so the buffer is safe to hand
   * to ImGui::InputText. Replaces std::strncpy, which MSVC deprecates as unsafe.
   *
   * @pre @p buffer_size > 0.
   */
  inline void copy_to_buffer(char *buffer, std::size_t buffer_size, std::string_view text) {
    const std::size_t count = std::min(text.size(), buffer_size - 1);
    std::copy_n(text.begin(), count, buffer);
    buffer[count] = '\0';
  }

} // namespace corundum::toolkit::widgets
