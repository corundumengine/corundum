// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace corundum::ui {

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

  float cursor_advance(const platform::Renderer &r, const PanelStyle &style) {
    return r.measure_text(style.family(FontRole::Ui).get(FontStyle::Regular), k_choice_cursor, style.font_size_body);
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

  void draw_option(platform::Renderer &r, const PanelStyle &style, std::string_view label, core::math::Vec2 pos,
                   bool selected, bool show_cursor) {
    // The advance is always cursor_advance (measured against k_choice_cursor) so the label
    // column lines up whether or not the option is selected or draws its cursor.
    const float cursor_w = cursor_advance(r, style);
    const std::uint32_t font_id = style.family(FontRole::Ui).get(FontStyle::Regular);
    const std::string_view cursor = (selected && show_cursor) ? k_choice_cursor : k_cursor_unselected;
    const core::math::Colour col = selected ? style.selected : style.choice;

    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = cursor,
        .position = pos,
        .char_size = style.font_size_body,
        .colour = col,
    });
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = label,
        .position = {.x = pos.x + cursor_w, .y = pos.y},
        .char_size = style.font_size_body,
        .colour = col,
    });
  }

} // namespace corundum::ui
