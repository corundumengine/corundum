#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <string_view>

namespace corundum::ui {

  /** @brief Fill @p rect with @p bg then draw @p border around it — the shared
   *         chrome of every nine-patch modal panel.
   *  @param r       Renderer; receives one DrawRect then the border's DrawSprite commands.
   *  @param bg      Panel fill colour (e.g. DialogBoxStyle::bg).
   *  @param border  Nine-patch frame. @pre border.texture_id != 0.
   *  @param pos     Top-left of the panel in screen pixels.
   *  @param size    Panel width/height in screen pixels.
   */
  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size);

  /** @brief Draw one selectable menu option: a "> " cursor (or two spaces when
   *         not selected) followed by @p label, coloured by selection state.
   *  @param r         Renderer; receives two DrawText commands (cursor, then label).
   *  @param style     Supplies font_id, font_size_body, and the selected/choice colours.
   *  @param label     Option text; drawn verbatim (no wrapping).
   *  @param pos       Top-left where the cursor starts.
   *  @param selected  True → "> " prefix + style.selected; false → "  " + style.choice.
   *  @return Horizontal advance of the cursor prefix in pixels, so callers can
   *          place the next column (`pos.x + return value` is the label's x).
   */
  float draw_option(platform::Renderer &r, const DialogBoxStyle &style, std::string_view label, core::math::Vec2 pos,
                    bool selected);

} // namespace corundum::ui
