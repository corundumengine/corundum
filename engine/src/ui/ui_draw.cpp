// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <cstddef>
#include <span>
#include <string_view>

namespace corundum::ui {

  PanelRect screen_panel_rect(core::math::Vec2 viewport, const PanelStyle &style, float top_inset) {
    const float width = std::min(viewport.x - (style.margin * 2.f), k_screen_panel_max_width);
    const float top = style.margin + top_inset;
    const float height = viewport.y - top - style.margin;
    return PanelRect{
        .pos = {.x = (viewport.x - width) * 0.5f, .y = top},
        .size = {.x = width, .y = height},
    };
  }

  int hovered_row(core::math::Vec2 cursor, const ListHit &list) noexcept {
    if (list.visible_rows <= 0 || list.row_height <= 0.f)
      return -1;
    if (cursor.x < list.row_pos.x || cursor.x > list.row_pos.x + list.row_width)
      return -1;
    const float offset = cursor.y - list.row_pos.y;
    if (offset < 0.f)
      return -1;
    const int index = static_cast<int>(std::floor(offset / list.row_height));
    if (index >= list.visible_rows)
      return -1;
    return list.first_row + index;
  }

  int hovered_row(core::math::Vec2 cursor, std::span<const RowRect> rows) noexcept {
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const RowRect &row = rows[i];
      if (cursor.x >= row.pos.x && cursor.x <= row.pos.x + row.width && cursor.y >= row.pos.y &&
          cursor.y <= row.pos.y + row.height)
        return static_cast<int>(i);
    }
    return -1;
  }

  int scroll_row_delta(float scroll_y) noexcept {
    // Truncation toward zero, and wheel-up is positive, so the sign flips to move focus up.
    return -static_cast<int>(scroll_y);
  }

  int clamp_scroll_to_cursor(int scroll, int cursor, int row_count, int visible_rows) noexcept {
    if (visible_rows <= 0 || row_count <= visible_rows)
      return 0;
    const int max_first = row_count - visible_rows;
    scroll = std::clamp(scroll, 0, max_first);
    if (cursor < scroll)
      scroll = cursor;
    else if (cursor >= scroll + visible_rows)
      scroll = cursor - visible_rows + 1;
    return std::clamp(scroll, 0, max_first);
  }

  float cursor_advance(const platform::Renderer &r, const PanelStyle &style, FontRole role) {
    return r.measure_text(style.family(role).get(FontStyle::Regular), k_choice_cursor, style.font_size_body);
  }

  void panel_fill(platform::Renderer &r, core::math::Colour bg, core::math::Vec2 pos, core::math::Vec2 size) {
    r.draw(platform::DrawRect{.position = pos, .size = size, .colour = bg});
  }

  void panel_frame(platform::Renderer &r, const NinePatchBorder &border, core::math::Vec2 pos, core::math::Vec2 size) {
    nine_patch_render(r, border, pos.x, pos.y, size.x, size.y);
  }

  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size) {
    panel_fill(r, bg, pos, size);
    panel_frame(r, border, pos, size);
  }

  void screen_backdrop(platform::Renderer &r, const PanelStyle &style, core::math::Vec2 viewport) {
    core::math::Colour bg = style.bg;
    bg.a = 255;
    panel_fill(r, bg, {.x = 0.f, .y = 0.f}, viewport);
  }

  void draw_option(platform::Renderer &r, const PanelStyle &style, std::string_view label, core::math::Vec2 pos,
                   bool selected, bool show_cursor) {
    draw_option(r, style, FontRole::Ui, FontStyle::Regular, label, pos, selected, show_cursor);
  }

  void draw_option(platform::Renderer &r, const PanelStyle &style, FontRole role, FontStyle label_style,
                   std::string_view label, core::math::Vec2 pos, bool selected, bool show_cursor) {
    // The advance always uses the role's regular face so the label column lines up whether or
    // not the label itself is bold, and whether or not the option is selected.
    const float cursor_w = cursor_advance(r, style, role);
    const FontFamily &family = style.family(role);
    const std::string_view cursor = (selected && show_cursor) ? k_choice_cursor : k_cursor_unselected;
    const core::math::Colour col = selected ? style.selected : style.choice;

    r.draw(platform::DrawText{
        .font_id = family.get(FontStyle::Regular),
        .text = cursor,
        .position = pos,
        .char_size = style.font_size_body,
        .colour = col,
    });
    r.draw(platform::DrawText{
        .font_id = family.get(label_style),
        .text = label,
        .position = {.x = pos.x + cursor_w, .y = pos.y},
        .char_size = style.font_size_body,
        .colour = col,
    });
  }

  namespace {

    template <class Run>
    float measure_styled_runs(const platform::Renderer &r, const FontFamily &family, std::span<const Run> runs,
                              unsigned char_size) {
      float width{0.f};
      for (const Run &run : runs)
        width += r.measure_text(family.get(run.style), run.text, char_size);
      return width;
    }

    template <class Run>
    void draw_styled_runs(platform::Renderer &r, const FontFamily &family, std::span<const Run> runs,
                          unsigned char_size, core::math::Colour colour, core::math::Vec2 pos) {
      float x = pos.x;
      for (const Run &run : runs) {
        if (run.text.empty())
          continue;
        r.draw(platform::DrawText{
            .font_id = family.get(run.style),
            .text = run.text,
            .position = {.x = x, .y = pos.y},
            .char_size = char_size,
            .colour = colour,
        });
        x += r.measure_text(family.get(run.style), run.text, char_size);
      }
    }

  } // namespace

  float measure_styled(const platform::Renderer &r, const FontFamily &family, std::span<const StyledRun> runs,
                       unsigned char_size) {
    return measure_styled_runs(r, family, runs, char_size);
  }

  float measure_styled(const platform::Renderer &r, const FontFamily &family, std::span<const StyledSegment> segments,
                       unsigned char_size) {
    return measure_styled_runs(r, family, segments, char_size);
  }

  void draw_styled(platform::Renderer &r, const FontFamily &family, std::span<const StyledRun> runs, unsigned char_size,
                   core::math::Colour colour, core::math::Vec2 pos) {
    draw_styled_runs(r, family, runs, char_size, colour, pos);
  }

  void draw_styled(platform::Renderer &r, const FontFamily &family, std::span<const StyledSegment> segments,
                   unsigned char_size, core::math::Colour colour, core::math::Vec2 pos) {
    draw_styled_runs(r, family, segments, char_size, colour, pos);
  }

} // namespace corundum::ui
