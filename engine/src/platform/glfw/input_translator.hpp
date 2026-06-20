#pragma once
#include <corundum/input/actions.hpp>

#include <array>

struct GLFWwindow;

namespace corundum::platform::glfw {

  /// Per-joystick axis activation state; used to detect rising/falling edges across frames.
  struct JoystickAxisState {
    std::array<bool, corundum::input::k_action_count> axis_active{};
  };

  /// Translate a GLFW key event into engine input state changes.
  /// Rising-edge detection: pressed[] is set only on the GLFW_PRESS transition.
  /// GLFW_REPEAT events are ignored.
  void translate_key(int key, int action, corundum::input::InputState &state) noexcept;

  /// Poll the first available GLFW joystick and update @p state with axis/button input.
  /// Call once per frame after glfwPollEvents().
  void poll_joystick(corundum::input::InputState &state, JoystickAxisState &axis) noexcept;

} // namespace corundum::platform::glfw
