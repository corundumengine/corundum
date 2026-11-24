// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/toolkit/host/tool_config.hpp>
#include <imgui.h>

namespace corundum::toolkit::widgets {

/// ImGui font handles loaded at startup.
struct FontHandles {
  ImFont *ui{nullptr};
  ImFont *icons{nullptr};
};

/** Load UI and optional icons fonts from ToolConfig paths.
 *
 *  Clears existing ImGui fonts, loads the UI font at `ui_size` and the
 *  icons font at `icons_size`. If the icons font path points to the same
 *  file, it is loaded with a different size and the full Unicode range.
 *
 *  @param config     ToolConfig with font_path and icons_font_path.
 *  @param ui_size    Font size for the UI font.
 *  @param icons_size Font size for the icons font.
 *  @return FontHandles struct (may have icon+UI, or just UI, or both null).
 */
[[nodiscard]] FontHandles load_tool_fonts(const corundum::toolkit::host::ToolConfig &config, float ui_size = 18.f,
                                          float icons_size = 26.f);

} // namespace corundum::toolkit::widgets
