// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/credits.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/credits/credits.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace corundum::gameplay::screens {

  namespace {
    constexpr float k_credits_pad_top = 72.f;
    constexpr float k_credits_pad_bottom = 112.f;
    constexpr float k_credits_section_gap = 28.f;
    constexpr float k_credits_heading_gap = 10.f;
    constexpr float k_credits_max_width = 900.f;

    float credits_heading_h(const ui::PanelStyle &style) {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_heading) + 6.f);
    }

    float credits_line_h(const ui::PanelStyle &style) {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    }
  } // namespace

  float credits_content_height(const platform::Renderer & /*r*/, const ui::PanelStyle &style,
                               const CreditsState &state) {
    const float heading_h = credits_heading_h(style);
    const float line_h = credits_line_h(style);

    float height = k_credits_pad_top;
    for (const credits::CreditsSection &section : state.sections) {
      if (!section.heading.empty())
        height += heading_h + k_credits_heading_gap;
      height += static_cast<float>(section.lines.size()) * line_h;
      height += k_credits_section_gap;
    }
    height += k_credits_pad_bottom;
    return height;
  }

  void credits_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport,
                            const CreditsState &state) {
    const float line_h = credits_line_h(style);
    const float heading_h = credits_heading_h(style);
    const std::uint32_t heading_font = style.family(ui::FontRole::Display).get(ui::FontStyle::Regular);
    const std::uint32_t body_font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    ui::screen_backdrop(r, style, viewport);

    const float content_w = std::min(std::max(viewport.x - (style.margin * 2.f), 0.f), k_credits_max_width);
    const float center_x = (viewport.x - content_w) * 0.5f;
    float y = k_credits_pad_top - state.scroll;

    for (const credits::CreditsSection &section : state.sections) {
      if (!section.heading.empty()) {
        const float width = r.measure_text(heading_font, section.heading, style.font_size_heading);
        r.draw(platform::DrawText{
            .font_id = heading_font,
            .text = section.heading,
            .position = {.x = center_x + ((content_w - width) * 0.5f), .y = y},
            .char_size = style.font_size_heading,
            .colour = style.speaker,
        });
        y += heading_h + k_credits_heading_gap;
      }
      for (const std::string &line : section.lines) {
        const float width = r.measure_text(body_font, line, style.font_size_body);
        r.draw(platform::DrawText{
            .font_id = body_font,
            .text = line,
            .position = {.x = center_x + ((content_w - width) * 0.5f), .y = y},
            .char_size = style.font_size_body,
            .colour = style.body,
        });
        y += line_h;
      }
      y += k_credits_section_gap;
    }
  }

} // namespace corundum::gameplay::screens
