// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/dialog_layout.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <string_view>

namespace corundum::ui {

  namespace {
    constexpr std::string_view k_cursor_unselected = "  ";
  } // namespace

  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size) {
    r.draw(platform::DrawRect{.position = pos, .size = size, .colour = bg});
    nine_patch_render(r, border, pos.x, pos.y, size.x, size.y);
  }

  float draw_option(platform::Renderer &r, const DialogBoxStyle &style, std::string_view label, core::math::Vec2 pos,
                    bool selected) {
    // Advance is always measured against k_choice_cursor so that the cursor column and
    // the label column line up whether the option is selected or not — even on a font
    // where k_choice_cursor and k_cursor_unselected happen to differ in width.
    const float cursor_w = r.measure_text(style.font_id, k_choice_cursor, style.font_size_body);
    const std::string_view cursor = selected ? k_choice_cursor : k_cursor_unselected;
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
    return cursor_w;
  }

} // namespace corundum::ui
