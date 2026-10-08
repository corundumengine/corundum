// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/credits/credits.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>

#include <string>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief Credits-screen state: the loaded title, sections and the current scroll offset.
   *
   *  The title is drawn large above the first section. Sections are loaded once when the Title
   *  opens (so the Title can hide the Credits row when there is nothing to show) and drawn
   *  top-to-bottom; the offset advances each fixed step. */
  struct CreditsState {
    std::string title{};

    std::vector<credits::CreditsSection> sections{};

    float scroll{};
  };

  /** @brief Total drawn height of the credits content, in logical pixels.
   *
   *  Independent of the viewport, so a caller can clamp the scroll and the render can advance
   *  without measuring the same text twice.
   */
  [[nodiscard]] float credits_content_height(const platform::Renderer &r, const ui::PanelStyle &style,
                                             const CreditsState &state);

  /** @brief Draw the credits over an opaque backdrop.
   *
   *  Pure render; invoked only while Credits is on top of the UI stack. The whole viewport is
   *  filled so the loaded world does not show through, then the sections are drawn centered,
   *  shifted up by CreditsState::scroll.
   *
   *  @param r        Renderer; emits the backdrop and the section text.
   *  @param style    Panel style supplying fonts, sizes and colours.
   *  @param state    Loaded sections and scroll offset.
   *  @param viewport Screen size in logical pixels.
   */
  void credits_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport,
                            const CreditsState &state);

} // namespace corundum::gameplay::screens
