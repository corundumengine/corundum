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

/** Step the shared confirmation modal: Back closes it without acting; Left/Right (or
 *  Up/Down) toggle the highlight; Activate runs on_yes when Yes is highlighted. The
 *  callback is moved out and the state cleared before it runs, so it may itself open a
 *  screen or reset the session without racing the modal it just answered.
 */
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

/** Draw the confirmation modal while Confirm is the top mode. Pure render; prompt_box_render
 *  owns the layout so there is nothing to share with hit-testing.
 */
void render_confirm(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                    core::math::Vec2 viewport) {
  if (engine.scene.mode() != Confirm)
    return;
  ui::prompt_box_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border,
                        gameplay.confirm.question, gameplay.confirm.yes_selected, viewport);
}

} // namespace corundum::gameplay::screens
