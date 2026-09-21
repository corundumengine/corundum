// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string_view>

namespace corundum::ui {

  /// Cursor prefix drawn before a selected menu option. Shared by the layout (which
  /// reserves exactly this column) and the renderer (which draws it and derives the
  /// column width from it), so the two never disagree.
  inline constexpr std::string_view k_choice_cursor = "> ";

  /// Two-space placeholder drawn in the cursor column before an unselected option or a
  /// wrapped continuation line, keeping every label in the same column.
  inline constexpr std::string_view k_cursor_unselected = "  ";

} // namespace corundum::ui
