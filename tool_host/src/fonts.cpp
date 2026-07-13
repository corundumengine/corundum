#include <corundum/tool_host/fonts.hpp>

#include <format>

namespace corundum::tool_host {

  FontHandles load_tool_fonts(const ToolConfig &config, float ui_size, float icons_size) {
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    FontHandles handles;
    if (!config.font_path.empty())
      handles.ui = io.Fonts->AddFontFromFileTTF(config.font_path.string().c_str(), ui_size);

    if (!config.icons_font_path.empty())
      handles.icons = io.Fonts->AddFontFromFileTTF(config.icons_font_path.string().c_str(), icons_size);

    return handles;
  }

} // namespace corundum::tool_host
