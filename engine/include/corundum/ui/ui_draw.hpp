// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <string_view>

namespace corundum::ui {

  /** @brief Horizontal advance of the choice cursor column — the x-offset at which a
   *         draw_option() label begins.
   *
   *  Measured against the selected cursor (k_choice_cursor) regardless of selection, so a
   *  selected and an unselected option reserve the same column. Callers that lay out
   *  several columns (prompt/inventory) use this instead of re-measuring the glyph, keeping
   *  the one definition of the advance beside draw_option().
   *
   *  @param r     Renderer used for font metrics.
   *  @param style Supplies font_id and font_size_body.
   *  @return Width in pixels reserved for the cursor prefix.
   */
  [[nodiscard]] float cursor_advance(const platform::Renderer &r, const DialogBoxStyle &style);

  /** @brief Fill @p rect with @p bg then draw @p border around it — the shared
   *         chrome of every nine-patch modal panel.
   *  @param r       Renderer; receives one DrawRect then the border's DrawSprite commands.
   *  @param bg      Panel fill colour (e.g. DialogBoxStyle::bg).
   *  @param border  Nine-patch frame; a zero texture_id or non-positive cell size skips only
   *                 the frame, leaving the fill rect intact.
   *  @param pos     Top-left of the panel in screen pixels.
   *  @param size    Panel width/height in screen pixels.
   */
  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size);

  /** @brief Draw one selectable menu option: a "> " cursor (or two spaces when
   *         @p show_cursor is false) followed by @p label, coloured by @p selected.
   *
   *  The label always begins at `pos.x + cursor_advance(r, style)`.
   *
   *  @param r           Renderer; receives two DrawText commands (cursor, then label).
   *  @param style       Supplies font_id, font_size_body, and the selected/choice colours.
   *  @param label       Option text; drawn verbatim (no wrapping).
   *  @param pos         Top-left where the cursor starts.
   *  @param selected    True → style.selected; false → style.choice.
   *  @param show_cursor True → "> " when selected, "  " otherwise; false → always "  ".
   *                     Pass false for continuation lines of a wrapped option so they keep
   *                     the selected colour without repeating the cursor.
   */
  void draw_option(platform::Renderer &r, const DialogBoxStyle &style, std::string_view label, core::math::Vec2 pos,
                   bool selected, bool show_cursor = true);

} // namespace corundum::ui
