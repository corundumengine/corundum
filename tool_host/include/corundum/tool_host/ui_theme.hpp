#pragma once
#include <expected>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>

namespace corundum::tool_host {

  enum class TextRole { Active, Disabled, Normal, Muted, Accent, Success, Warning, Error };

  struct ThemeColors {
    ImVec4 text_normal;
    ImVec4 text_muted;
    ImVec4 text_accent;
    ImVec4 text_success;
    ImVec4 text_warning;
    ImVec4 text_error;
    ImVec4 text_active;
    ImVec4 text_disabled;
  };

  [[nodiscard]] ThemeColors ApplyEditorThemeRefined();
  [[nodiscard]] ThemeColors ApplyThemeFromJson(const nlohmann::json &j);
  [[nodiscard]] std::expected<ThemeColors, std::string> load_theme(const std::string &path);
  [[nodiscard]] ImVec4 GetTextColor(const ThemeColors &theme, TextRole role);
  void TextColoredRole(const ThemeColors &theme, TextRole role, const char *text);

  struct ScopedTextColor {
    explicit ScopedTextColor(ImVec4 color);
    ~ScopedTextColor();
  };

} // namespace corundum::tool_host
