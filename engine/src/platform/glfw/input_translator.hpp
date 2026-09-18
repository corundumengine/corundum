// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>

namespace corundum::platform::glfw {

  /// Translate a GLFW key event into engine input state changes.
  /// Rising-edge detection is delegated to @p resolver, so releasing one binding of
  /// an Action never clears another binding that is still held. GLFW_REPEAT is ignored.
  void translate_key(int key, int action, corundum::input::ActionResolver &resolver,
                     corundum::input::InputState &state) noexcept;

  /// Translate a GLFW mouse button event into engine input state changes.
  /// Same rising-edge semantics as translate_key. Left click maps to Action::Select,
  /// reused (not a parallel concept) so click-to-move and NPC interaction share one signal.
  void translate_mouse_button(int button, int action, corundum::input::ActionResolver &resolver,
                              corundum::input::InputState &state) noexcept;

  /// Accumulate a GLFW scroll event into InputState::scroll_delta_y.
  void translate_scroll(double yoffset, corundum::input::InputState &state) noexcept;

  /// Fold the first connected gamepad, if any, into @p state via @p resolver.
  /// Call once per frame after glfwPollEvents(). A gamepad that is missing or unmapped
  /// has its sources released, so a disconnect cannot latch an action as held.
  void poll_gamepad(corundum::input::ActionResolver &resolver, corundum::input::InputState &state) noexcept;

  /// Raise Action::Quit from a window-manager close request. Routed through the
  /// translator (rather than writing InputState directly) so its source is tracked
  /// like any other binding.
  void translate_window_close(corundum::input::ActionResolver &resolver, corundum::input::InputState &state) noexcept;

} // namespace corundum::platform::glfw
