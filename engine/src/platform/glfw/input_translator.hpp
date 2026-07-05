#pragma once
#include <corundum/input/actions.hpp>

#include <array>

struct GLFWwindow;

namespace corundum::platform::glfw {

  /// Per-joystick axis/button activation state; used to detect rising/falling edges
  /// across frames, and to tell whether the joystick (vs. another input device sharing
  /// the same Action) was the one currently driving a given action's held state.
  struct JoystickAxisState {
    std::array<bool, corundum::input::k_action_count> axis_active{};
    std::array<bool, corundum::input::k_action_count> button_active{};
  };

  /// Translate a GLFW key event into engine input state changes.
  /// Rising-edge detection: pressed[] is set only on the GLFW_PRESS transition.
  /// GLFW_REPEAT events are ignored.
  void translate_key(int key, int action, corundum::input::InputState &state) noexcept;

  /// Translate a GLFW mouse button event into engine input state changes.
  /// Same rising-edge semantics as translate_key. Left click maps to Action::Select,
  /// reused (not a parallel concept) so click-to-move and NPC interaction share one signal.
  void translate_mouse_button(int button, int action, corundum::input::InputState &state) noexcept;

  /// Accumulate a GLFW scroll event into InputState::scroll_delta_y.
  void translate_scroll(double yoffset, corundum::input::InputState &state) noexcept;

  /// Poll the first available GLFW joystick and update @p state with axis/button input.
  /// Call once per frame after glfwPollEvents().
  void poll_joystick(corundum::input::InputState &state, JoystickAxisState &axis) noexcept;

} // namespace corundum::platform::glfw
