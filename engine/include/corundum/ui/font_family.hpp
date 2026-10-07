// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace corundum::ui {

  /// A font's purpose within the game. Every role is loaded from its own family of files,
  /// so dialogue, quest, UI and display text can each use a distinct typeface.
  ///
  /// Display is the only optional role: a game may omit it, in which case display text draws in
  /// the UI family. The other three roles are required.
  enum class FontRole : std::uint8_t {
    Dialogue,
    Quest,
    Ui,
    Display,
  };

  /// One weight/style variant within a FontFamily. Files for Bold, Italic and BoldItalic are
  /// optional; a family without them falls back to the closest present style.
  enum class FontStyle : std::uint8_t {
    Regular,
    Bold,
    Italic,
    BoldItalic,
  };

  /// Number of entries in a role-indexed family array.
  constexpr std::size_t k_font_role_count{4};

  /// Number of entries in a FontFamily's style-indexed id array.
  constexpr std::size_t k_font_style_count{4};

  /// The renderer's font ids for one role, indexed by FontStyle. A fully loaded family has no
  /// zero slots; a default-constructed family is all zeros and represents "not loaded" (the
  /// loader reserves id 0 as an invalid sentinel).
  struct FontFamily {
    std::array<std::uint32_t, k_font_style_count> ids{};

    /// @return The renderer id for @p style, or 0 when the family is unloaded.
    [[nodiscard]] std::uint32_t get(FontStyle style) const noexcept {
      return ids[static_cast<std::size_t>(style)];
    }
  };

  /// Config-supplied file names for one role, relative to ResourcePaths::font_dir.
  struct FontFamilyPaths {
    /** @brief Bold file name; empty when the family has no bold face. */
    std::string bold{};

    /** @brief Bold-italic file name; empty when the family has no bold-italic face. */
    std::string bold_italic{};

    /** @brief Italic file name; empty when the family has no italic face. */
    std::string italic{};

    /** @brief Regular file name; required for every family. */
    std::string regular{};
  };

} // namespace corundum::ui
