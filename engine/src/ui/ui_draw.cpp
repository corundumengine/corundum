// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <string_view>

namespace corundum::ui {

  float cursor_advance(const platform::Renderer &r, const DialogBoxStyle &style) {
    return r.measure_text(style.font_id, k_choice_cursor, style.font_size_body);
  }

  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size) {
    r.draw(platform::DrawRect{.position = pos, .size = size, .colour = bg});
    nine_patch_render(r, border, pos.x, pos.y, size.x, size.y);
  }

  void draw_option(platform::Renderer &r, const DialogBoxStyle &style, std::string_view label, core::math::Vec2 pos,
                   bool selected, bool show_cursor) {
    // The advance is always cursor_advance (measured against k_choice_cursor) so the label
    // column lines up whether or not the option is selected or draws its cursor.
    const float cursor_w = cursor_advance(r, style);
    const std::string_view cursor = (selected && show_cursor) ? k_choice_cursor : k_cursor_unselected;
    const core::math::Colour col = selected ? style.selected : style.choice;

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = cursor,
        .position = pos,
        .char_size = style.font_size_body,
        .colour = col,
    });
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = label,
        .position = {.x = pos.x + cursor_w, .y = pos.y},
        .char_size = style.font_size_body,
        .colour = col,
    });
  }

} // namespace corundum::ui
