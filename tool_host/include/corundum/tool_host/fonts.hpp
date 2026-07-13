#pragma once
#include <corundum/tool_host/tool_config.hpp>
#include <imgui.h>

namespace corundum::tool_host {

  /// ImGui font handles loaded at startup.
  struct FontHandles {
    ImFont *ui{nullptr};
    ImFont *icons{nullptr};
  };

  /** @brief Load UI and optional icons fonts from ToolConfig paths.
   *
   * Clears existing ImGui fonts, loads the UI font at @p ui_size and the
   * icons font at @p icons_size. If the icons font path points to the same
   * file, it is loaded with a different size and the full Unicode range.
   *
   * @param[in]  config     ToolConfig with font_path and icons_font_path.
   * @param[in]  ui_size    Font size for the UI font.
   * @param[in]  icons_size Font size for the icons font.
   * @return FontHandles struct (may have icon+UI, or just UI, or both null).
   */
  [[nodiscard]] FontHandles load_tool_fonts(const ToolConfig &config, float ui_size = 18.f, float icons_size = 26.f);

} // namespace corundum::tool_host
