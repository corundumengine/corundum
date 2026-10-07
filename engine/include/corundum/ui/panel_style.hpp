// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <array>
#include <cstddef>

namespace corundum::ui {

  /// Visual configuration shared by every screen panel, prompt, toast and menu. Pure data — no
  /// behaviour. Owned by RenderState so a single settings change restyles every panel at once.
  struct PanelStyle {
    /** @brief Per-role font families, indexed by FontRole. */
    std::array<FontFamily, k_font_role_count> fonts{};

    unsigned font_size_speaker{26};

    unsigned font_size_body{22};

    unsigned font_size_prompt{18};

    /** @brief Size for static headings drawn in the display family. */
    unsigned font_size_heading{48};

    /** @brief Size for transient banners drawn in the display family. */
    unsigned font_size_banner{64};

    float margin{20.f};

    float line_spacing{32.f};

    float panel_height_frac{0.32f};

    core::math::Colour bg{.r = 0, .g = 0, .b = 0, .a = 200};

    core::math::Colour speaker{.r = 255, .g = 255, .b = 0, .a = 255};

    core::math::Colour body{.r = 255, .g = 255, .b = 255, .a = 255};

    core::math::Colour choice{.r = 200, .g = 200, .b = 200, .a = 255};

    core::math::Colour selected{.r = 255, .g = 255, .b = 0, .a = 255};

    /// @return The family for @p role.
    [[nodiscard]] const FontFamily &family(FontRole role) const noexcept {
      return fonts[static_cast<std::size_t>(role)];
    }
  };

  /// The panel style plus the nine-patch border texture every panel frame draws with.
  struct PanelSkin {
    PanelStyle style{};

    NinePatchBorder border{};
  };

} // namespace corundum::ui
