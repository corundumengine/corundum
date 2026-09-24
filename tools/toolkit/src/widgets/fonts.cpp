// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/toolkit/host/tool_config.hpp>
#include <corundum/toolkit/widgets/fonts.hpp>
#include <imgui.h>

namespace corundum::toolkit::widgets {

  FontHandles load_tool_fonts(const corundum::toolkit::host::ToolConfig &config, float ui_size, float icons_size) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    FontHandles handles;
    if (!config.font_path.empty())
      handles.ui = io.Fonts->AddFontFromFileTTF(config.font_path.string().c_str(), ui_size);

    if (!config.icons_font_path.empty())
      handles.icons = io.Fonts->AddFontFromFileTTF(config.icons_font_path.string().c_str(), icons_size);

    return handles;
  }

} // namespace corundum::toolkit::widgets
