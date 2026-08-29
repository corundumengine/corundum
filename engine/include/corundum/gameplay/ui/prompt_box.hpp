#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/ui/dialog_box.hpp>
#include <corundum/gameplay/ui/nine_patch.hpp>
#include <corundum/platform/renderer.hpp>

#include <string_view>

namespace corundum::gameplay::ui {

  /** @brief Draw a centered modal confirm box with a navigable Yes/No highlight.
   *
   * Renders `question` centered on the top line and a `> Yes   No` row below; the
   * highlighted option uses `style.selected` and the other uses `style.choice`.
   * Pure render — no state, no measurement caching. The caller is responsible for
   * only invoking this while a prompt is active.
   *
   * @param r             Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   * @param style         Dialog text style (font id/sizes/colours); reused so prompt boxes match dialogue.
   * @param border        Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   * @param question      Single-line question text (e.g. "Enter?" or "Leave?").
   * @param yes_selected  True if the Yes option is highlighted; false for No. Navigation is
   *                      Left/Right (Up/Down aliased); Select commits the highlighted option,
   *                      Cancel always backs out.
   * @param viewport      Screen size in pixels; the box is centered within this.
   */
  void prompt_box_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                         std::string_view question, bool yes_selected, core::math::Vec2 viewport);

} // namespace corundum::gameplay::ui
