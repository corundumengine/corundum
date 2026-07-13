#include <corundum/core/json_io.hpp>
#include <corundum/tool_host/ui_theme.hpp>
#include <imgui.h>

#include <fstream>

namespace corundum::tool_host {

  static ImVec4 HexToImVec4(const std::string &hex) {
    std::string h = hex[0] == '#' ? hex.substr(1) : hex;
    uint32_t value = std::stoul(h, nullptr, 16);
    float r{}, g{}, b{}, a{1.0f};
    if (h.length() == 6) {
      r = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
      g = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
      b = static_cast<float>((value >> 0) & 0xFF) / 255.0f;
    } else {
      r = static_cast<float>((value >> 24) & 0xFF) / 255.0f;
      g = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
      b = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
      a = static_cast<float>((value >> 0) & 0xFF) / 255.0f;
    }
    return ImVec4{r, g, b, a};
  }

  ThemeColors ApplyEditorThemeRefined() {
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *c = style.Colors;
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    c[ImGuiCol_Text] = ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.095f, 0.10f, 0.105f, 1.00f);
    const ImVec4 primary = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
    c[ImGuiCol_Button] = primary;
    c[ImGuiCol_ButtonHovered] = ImVec4(0.36f, 0.69f, 1.0f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.50f, 0.90f, 1.0f);
    ThemeColors theme;
    theme.text_normal = c[ImGuiCol_Text];
    theme.text_muted = ImVec4(0.5f, 0.52f, 0.55f, 1.0f);
    theme.text_accent = primary;
    theme.text_success = ImVec4(0.30f, 0.80f, 0.45f, 1.0f);
    theme.text_warning = ImVec4(0.95f, 0.65f, 0.20f, 1.0f);
    theme.text_error = ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    theme.text_active = primary;
    theme.text_disabled = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    return theme;
  }

  ThemeColors ApplyThemeFromJson(const nlohmann::json &j) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *c = style.Colors;
    auto get = [&](const char *key) { return HexToImVec4(j["colors"].at(key)); };
    c[ImGuiCol_Text] = get("text");
    c[ImGuiCol_WindowBg] = get("window_bg");
    c[ImGuiCol_PopupBg] = get("popup_bg");
    c[ImGuiCol_Border] = get("border");
    c[ImGuiCol_Button] = get("primary");
    c[ImGuiCol_ButtonHovered] = get("primary_hover");
    c[ImGuiCol_ButtonActive] = get("primary_active");
    c[ImGuiCol_FrameBg] = get("frame_bg");
    c[ImGuiCol_FrameBgHovered] = get("frame_bg_hovered");
    c[ImGuiCol_FrameBgActive] = get("frame_bg_active");
    c[ImGuiCol_Header] = get("header");
    c[ImGuiCol_HeaderHovered] = get("header_hovered");
    c[ImGuiCol_HeaderActive] = get("header_active");
    const auto &t = j["text"];
    ThemeColors theme;
    theme.text_active = HexToImVec4(t.at("active"));
    theme.text_disabled = HexToImVec4(t.at("disabled"));
    theme.text_normal = HexToImVec4(t.at("normal"));
    theme.text_muted = HexToImVec4(t.at("muted"));
    theme.text_accent = HexToImVec4(t.at("accent"));
    theme.text_success = HexToImVec4(t.at("success"));
    theme.text_warning = HexToImVec4(t.at("warning"));
    theme.text_error = HexToImVec4(t.at("error"));
    return theme;
  }

  std::expected<ThemeColors, std::string> load_theme(const std::string &path) {
    auto j = corundum::core::read_json(path);
    if (!j)
      return std::unexpected(std::string("Cannot open theme: ") + j.error());
    ImGui::StyleColorsDark();
    return ApplyThemeFromJson(*j);
  }

  ImVec4 GetTextColor(const ThemeColors &theme, TextRole role) {
    switch (role) {
    case TextRole::Active:
      return theme.text_active;
    case TextRole::Disabled:
      return theme.text_disabled;
    case TextRole::Normal:
      return theme.text_normal;
    case TextRole::Muted:
      return theme.text_muted;
    case TextRole::Accent:
      return theme.text_accent;
    case TextRole::Success:
      return theme.text_success;
    case TextRole::Warning:
      return theme.text_warning;
    case TextRole::Error:
      return theme.text_error;
    }
    return theme.text_normal;
  }

  void TextColoredRole(const ThemeColors &theme, TextRole role, const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, GetTextColor(theme, role));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
  }

  ScopedTextColor::ScopedTextColor(ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
  }

  ScopedTextColor::~ScopedTextColor() {
    ImGui::PopStyleColor();
  }

} // namespace corundum::tool_host
