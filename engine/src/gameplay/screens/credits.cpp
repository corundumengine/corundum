// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/credits.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/credits/credits.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

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

float credits_content_height(const platform::Renderer & /*r*/, const ui::PanelStyle &style, const CreditsState &state) {
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

namespace {

/// The centered text column the roll draws into, in screen pixels.
struct CreditsColumn {
  float center_x{};

  float width{};
};

/// The face, size and colour one kind of credits line (title, section heading, body) draws in.
struct CreditsTextStyle {
  platform::FontId font{platform::FontId::Invalid};

  unsigned char_size{};

  core::math::Colour colour{};
};

/// Draw @p text centered in @p column at @p y.
void draw_centered_line(platform::Renderer &r, const CreditsColumn &column, const CreditsTextStyle &text_style,
                        std::string_view text, float y) {
  const float width = r.measure_text(text_style.font, text, text_style.char_size);
  r.draw(platform::DrawText{
      .font_id = text_style.font,
      .text = text,
      .position = {.x = column.center_x + ((column.width - width) * 0.5f), .y = y},
      .char_size = text_style.char_size,
      .colour = text_style.colour,
  });
}

/// Cover-scale the parchment background across the whole viewport, never cropping it narrower
/// than the roll. @return Whether a background drew; the roll switches to dark ink when one did.
bool draw_credits_background(platform::Renderer &r, core::math::Vec2 viewport, const CreditsState &state) {
  const core::math::Vec2 size = state.background_size;
  if (state.background_texture == platform::TextureId::Invalid || size.x <= 0.f || size.y <= 0.f)
    return false;

  const float scale = std::max(viewport.x / size.x, viewport.y / size.y);
  const float position_x = (viewport.x - (size.x * scale)) * 0.5f;
  const float position_y = (viewport.y - (size.y * scale)) * 0.5f;
  r.draw(platform::DrawSprite{
      .texture_id = state.background_texture,
      .position = {.x = position_x, .y = position_y},
      .source = {.x = 0, .y = 0, .width = static_cast<int>(size.x), .height = static_cast<int>(size.y)},
      .scale = {.x = scale, .y = scale},
  });
  return true;
}

} // namespace

void credits_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport,
                          const CreditsState &state) {
  ui::screen_backdrop(r, style, viewport);
  const bool has_background = draw_credits_background(r, viewport, state);

  const CreditsTextStyle title_style{
      .font = style.family(ui::FontRole::Display).get(ui::FontStyle::Regular),
      .char_size = style.font_size_heading,
      .colour = has_background ? k_ink_title : style.speaker,
  };
  const CreditsTextStyle heading_style{
      .font = title_style.font,
      .char_size = style.font_size_speaker,
      .colour = has_background ? k_ink_heading : style.selected,
  };
  const CreditsTextStyle body_style{
      .font = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular),
      .char_size = style.font_size_body,
      .colour = has_background ? k_ink_body : style.body,
  };

  const float content_w = std::min(std::max(viewport.x - (style.margin * 2.f), 0.f), k_credits_max_width);
  const CreditsColumn column{.center_x = (viewport.x - content_w) * 0.5f, .width = content_w};
  const float heading_h = credits_heading_h(style);
  const float section_heading_h = credits_section_heading_h(style);
  const float line_h = credits_line_h(style);

  float y = (viewport.y * 0.5f) - state.scroll;
  if (!state.title.empty()) {
    draw_centered_line(r, column, title_style, state.title, y);
    y += heading_h + k_credits_section_gap;
  }

  for (const credits::CreditsSection &section : state.sections) {
    if (!section.heading.empty()) {
      draw_centered_line(r, column, heading_style, section.heading, y);
      y += section_heading_h + k_credits_heading_gap;
    }
    for (const std::string &line : section.lines) {
      draw_centered_line(r, column, body_style, line, y);
      y += line_h;
    }
    y += k_credits_section_gap;
  }
}

} // namespace corundum::gameplay::screens
