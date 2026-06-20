#pragma once

#include <expected>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <string>

namespace tools::theme {

  /**
   * @brief Semantic text color roles for the editor UI.
   */
  enum class TextRole { Active, Disabled, Normal, Muted, Accent, Success, Warning, Error };

  /**
   * @brief Semantic text colors resolved from a loaded theme.
   *
   * Returned by load_theme() and ApplyEditorThemeRefined(); stored by the
   * caller and passed explicitly to render functions that need color lookups.
   */
  struct ThemeColors {
    ImVec4 text_normal;   ///< Default body text.
    ImVec4 text_muted;    ///< Subdued / secondary text.
    ImVec4 text_accent;   ///< Brand / highlight color.
    ImVec4 text_success;  ///< Success / confirmation.
    ImVec4 text_warning;  ///< Warning / caution.
    ImVec4 text_error;    ///< Error / destructive action.
    ImVec4 text_active;   ///< Active / selected item.
    ImVec4 text_disabled; ///< Disabled / unavailable.
  };

  // ----------------------------
  // Theme application
  // ----------------------------

  /**
   * @brief Apply a built-in dark fallback theme to the current ImGui context.
   *
   * Calls ImGui::StyleColorsDark() internally and populates semantic colors
   * with sensible defaults.
   *
   * @return Semantic ThemeColors matching the applied style.
   */
  [[nodiscard]] ThemeColors ApplyEditorThemeRefined();

  /**
   * @brief Apply colors from a parsed JSON theme object to the current ImGui context.
   *
   * @param j Parsed JSON root object containing "colors" and "text" sub-objects.
   * @return Semantic ThemeColors extracted from the JSON.
   */
  [[nodiscard]] ThemeColors ApplyThemeFromJson(const nlohmann::json &j);

  /**
   * @brief Load a JSON theme file and apply it to the current ImGui context.
   *
   * @param path Path to the JSON theme file.
   * @return ThemeColors on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<ThemeColors, std::string> load_theme(const std::string &path);

  // ----------------------------
  // Semantic color access
  // ----------------------------

  /**
   * @brief Retrieve a color from a ThemeColors instance by semantic role.
   *
   * @param theme Theme to query.
   * @param role  Semantic text role.
   * @return The corresponding ImVec4 color.
   */
  [[nodiscard]] ImVec4 GetTextColor(const ThemeColors &theme, TextRole role);

  /**
   * @brief Render @p text using the color for @p role from @p theme.
   *
   * @param theme Theme to query.
   * @param role  Semantic text role.
   * @param text  Null-terminated string to render.
   */
  void TextColoredRole(const ThemeColors &theme, TextRole role, const char *text);

  /**
   * @brief RAII guard that pushes/pops an ImGui text color.
   */
  struct ScopedTextColor {
    /// @brief Push @p color as the active ImGui text color.
    explicit ScopedTextColor(ImVec4 color);
    /// @brief Pop the pushed color.
    ~ScopedTextColor();
  };

} // namespace tools::theme
