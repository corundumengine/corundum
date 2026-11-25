// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// Cursor and pointer helpers shared by the gameplay screens' update handlers.

#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/ui/ui_draw.hpp>

namespace corundum::gameplay::screens {

/** Modulo wrap of a UI list cursor, matching the dialogue choice cursor: Down past the last
 *  row lands on the first, Up past the first lands on the last. `count` must be > 0.
 */
inline int wrap_cursor(int current, int delta, int count) noexcept {
  return (current + delta + count) % count;
}

/** The window's size in logical points, the space panels and the cursor share. */
inline core::math::Vec2 screen_viewport(const Engine &engine) noexcept {
  const auto [width, height] = engine.window().size();
  return {.x = static_cast<float>(width), .y = static_cast<float>(height)};
}

/** Whether `cursor` lies inside the half-open screen-space `rect`. */
inline bool cursor_in(core::math::Vec2 cursor, const ui::RowRect &rect) noexcept {
  return cursor.x >= rect.pos.x && cursor.x <= rect.pos.x + rect.width && cursor.y >= rect.pos.y &&
         cursor.y <= rect.pos.y + rect.height;
}

/** The cursor's current position as a point. */
inline core::math::Vec2 intent_cursor(const input::InputIntent &intent) noexcept {
  return {.x = intent.cursor_x, .y = intent.cursor_y};
}

/** True when the step carries pointer or wheel motion a screen should react to. */
inline bool pointer_active(const input::InputIntent &intent) noexcept {
  return intent.mouse_moved || intent.cursor_clicked || intent.scroll_y != 0.f;
}

/** True when the cursor should re-focus the row it is over: a real move or a click. */
inline bool pointer_focus(const input::InputIntent &intent) noexcept {
  return intent.mouse_moved || intent.cursor_clicked;
}

} // namespace corundum::gameplay::screens
