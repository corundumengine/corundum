// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/confirm.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/ui/prompt_box.hpp>

#include "gameplay/screens/screen_handlers.hpp"

#include <functional>
#include <utility>

namespace corundum::gameplay::screens {

void update_confirm(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  ConfirmState &state = gameplay.confirm;
  if (intent.back) {
    state = {};
    engine.scene.ui.pop();
    return;
  }
  if (intent.navigate_x != 0 || intent.navigate_y != 0)
    state.yes_selected = !state.yes_selected;
  if (!intent.activate)
    return;

  const bool confirmed = state.yes_selected;
  const std::function<void(Gameplay &)> on_yes = std::move(state.on_yes);
  state = {};
  engine.scene.ui.pop();
  if (confirmed && on_yes)
    on_yes(gameplay);
}

void render_confirm(const Engine &engine, const ConfirmState &state, platform::Renderer &renderer,
                    core::math::Vec2 viewport) {
  if (engine.scene.mode() != Confirm)
    return;
  ui::prompt_box_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, state.question,
                        state.yes_selected, viewport);
}

} // namespace corundum::gameplay::screens
