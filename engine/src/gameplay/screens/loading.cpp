// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/loading.hpp>

#include "gameplay/screens/screen_handlers.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <string_view>

namespace corundum::gameplay::screens {

void loading_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style, core::math::Vec2 viewport) {
  constexpr std::string_view k_loading_text = "Loading...";
  const platform::FontId font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

  ui::screen_backdrop(renderer, style, viewport);

  const float width = renderer.measure_text(font_id, k_loading_text, style.font_size_speaker);
  const float height = static_cast<float>(style.font_size_speaker);
  renderer.draw(platform::DrawText{
      .font_id = font_id,
      .text = k_loading_text,
      .position = {.x = (viewport.x - width) * 0.5f, .y = (viewport.y - height) * 0.5f},
      .char_size = style.font_size_speaker,
      .colour = style.speaker,
  });
}

/** Step the Loading overlay: once the overlay has actually rendered, run the queued load. */
void update_loading(Gameplay &gameplay) {
  gameplay.advance_loading();
}

/** Draw "Loading..." and record that it happened, so the next step may run the queued work. */
void render_loading(const Engine &engine, Gameplay &gameplay, platform::Renderer &renderer, core::math::Vec2 viewport) {
  if (engine.scene.mode() != Loading)
    return;
  loading_panel_render(renderer, engine.render.panel_skin.style, viewport);
  gameplay.notify_loading_rendered();
}

} // namespace corundum::gameplay::screens
