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
    constexpr float k_credits_section_gap = 28.f;
    constexpr float k_credits_heading_gap = 10.f;
    constexpr float k_credits_max_width = 900.f;

    constexpr core::math::Colour k_ink_title{.r = 90, .g = 30, .b = 25, .a = 255};
    constexpr core::math::Colour k_ink_heading{.r = 120, .g = 35, .b = 30, .a = 255};
    constexpr core::math::Colour k_ink_body{.r = 55, .g = 40, .b = 28, .a = 255};

    float credits_heading_h(const ui::PanelStyle &style) {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_heading) + 6.f);
    }

    float credits_section_heading_h(const ui::PanelStyle &style) {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_speaker) + 6.f);
    }

    float credits_line_h(const ui::PanelStyle &style) {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    }
  } // namespace

  float credits_content_height(const platform::Renderer & /*r*/, const ui::PanelStyle &style,
                               const CreditsState &state) {
    const float heading_h = credits_heading_h(style);
    const float section_heading_h = credits_section_heading_h(style);
    const float line_h = credits_line_h(style);

    float height = 0.f;
    if (!state.title.empty())
      height += heading_h + k_credits_section_gap;
    for (const credits::CreditsSection &section : state.sections) {
      if (!section.heading.empty())
        height += section_heading_h + k_credits_heading_gap;
      height += static_cast<float>(section.lines.size()) * line_h;
      height += k_credits_section_gap;
    }
    return height;
  }

  void credits_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport,
                            const CreditsState &state) {
    const float line_h = credits_line_h(style);
    const float heading_h = credits_heading_h(style);
    const float section_heading_h = credits_section_heading_h(style);
    const std::uint32_t heading_font = style.family(ui::FontRole::Display).get(ui::FontStyle::Regular);
    const std::uint32_t body_font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    ui::screen_backdrop(r, style, viewport);

    const core::math::Vec2 size = state.background_size;
    const bool has_background = state.background_texture != 0 && size.x > 0.f && size.y > 0.f;
    const core::math::Colour title_colour = has_background ? k_ink_title : style.speaker;
    const core::math::Colour heading_colour = has_background ? k_ink_heading : style.selected;
    const core::math::Colour body_colour = has_background ? k_ink_body : style.body;
    if (has_background) {
      const float scale = std::max(viewport.x / size.x, viewport.y / size.y);
      const float position_x = (viewport.x - (size.x * scale)) * 0.5f;
      const float position_y = (viewport.y - (size.y * scale)) * 0.5f;
      const int source_w = static_cast<int>(size.x);
      const int source_h = static_cast<int>(size.y);
      r.draw(platform::DrawSprite{
          .texture_id = state.background_texture,
          .position = {.x = position_x, .y = position_y},
          .source = {.x = 0, .y = 0, .width = source_w, .height = source_h},
          .scale = {.x = scale, .y = scale},
      });
    }

    const float content_w = std::min(std::max(viewport.x - (style.margin * 2.f), 0.f), k_credits_max_width);
    const float center_x = (viewport.x - content_w) * 0.5f;
    float y = (viewport.y * 0.5f) - state.scroll;

    if (!state.title.empty()) {
      const float width = r.measure_text(heading_font, state.title, style.font_size_heading);
      r.draw(platform::DrawText{
          .font_id = heading_font,
          .text = state.title,
          .position = {.x = center_x + ((content_w - width) * 0.5f), .y = y},
          .char_size = style.font_size_heading,
          .colour = title_colour,
      });
      y += heading_h + k_credits_section_gap;
    }

    for (const credits::CreditsSection &section : state.sections) {
      if (!section.heading.empty()) {
        const float width = r.measure_text(heading_font, section.heading, style.font_size_speaker);
        r.draw(platform::DrawText{
            .font_id = heading_font,
            .text = section.heading,
            .position = {.x = center_x + ((content_w - width) * 0.5f), .y = y},
            .char_size = style.font_size_speaker,
            .colour = heading_colour,
        });
        y += section_heading_h + k_credits_heading_gap;
      }
      for (const std::string &line : section.lines) {
        const float width = r.measure_text(body_font, line, style.font_size_body);
        r.draw(platform::DrawText{
            .font_id = body_font,
            .text = line,
            .position = {.x = center_x + ((content_w - width) * 0.5f), .y = y},
            .char_size = style.font_size_body,
            .colour = body_colour,
        });
        y += line_h;
      }
      y += k_credits_section_gap;
    }
  }

} // namespace corundum::gameplay::screens
