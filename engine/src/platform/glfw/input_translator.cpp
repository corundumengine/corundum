#include "input_translator.hpp"

#include <GLFW/glfw3.h>

#include <array>

namespace corundum::platform::glfw {

  namespace {

    using corundum::input::Action;

    struct KeyBinding {
      int glfw_key;
      Action action;
    };

    constexpr std::array<KeyBinding, 12> k_default_bindings{{
        {GLFW_KEY_W, Action::MoveUp},
        {GLFW_KEY_S, Action::MoveDown},
        {GLFW_KEY_A, Action::MoveLeft},
        {GLFW_KEY_D, Action::MoveRight},
        {GLFW_KEY_UP, Action::MoveUp},
        {GLFW_KEY_DOWN, Action::MoveDown},
        {GLFW_KEY_LEFT, Action::MoveLeft},
        {GLFW_KEY_RIGHT, Action::MoveRight},
        {GLFW_KEY_ENTER, Action::Select},
        {GLFW_KEY_SPACE, Action::Select},
        {GLFW_KEY_ESCAPE, Action::Cancel},
        {GLFW_KEY_Q, Action::Quit},
    }};

    struct ButtonBinding {
      int button;
      Action action;
    };

    // Standard Xbox/PlayStation layout: A/Cross=0, B/Circle=1, Start=7
    constexpr std::array<ButtonBinding, 3> k_button_bindings{{
        {0, Action::Select},
        {1, Action::Cancel},
        {7, Action::Quit},
    }};

    struct MouseBinding {
      int button;
      Action action;
    };

    constexpr std::array<MouseBinding, 1> k_mouse_bindings{{
        {GLFW_MOUSE_BUTTON_LEFT, Action::Select},
    }};

    // GLFW axis range is -1.0 to +1.0.
    inline constexpr float k_axis_deadzone = 0.5f;

  } // namespace

  void translate_key(int key, int action, corundum::input::InputState &state) noexcept {
    if (action == GLFW_REPEAT)
      return;

    const bool down = (action == GLFW_PRESS);
    for (const auto &[binding_key, act] : k_default_bindings) {
      if (binding_key != key)
        continue;
      const auto idx = static_cast<std::size_t>(act);
      if (down && !state.held[idx])
        state.pressed[idx] = true; // rising edge only
      state.held[idx] = down;
      break;
    }
  }

  void translate_mouse_button(int button, int action, corundum::input::InputState &state) noexcept {
    if (action == GLFW_REPEAT)
      return;

    const bool down = (action == GLFW_PRESS);
    for (const auto &[binding_button, act] : k_mouse_bindings) {
      if (binding_button != button)
        continue;
      const std::size_t idx = static_cast<std::size_t>(act);
      if (down && !state.held[idx])
        state.pressed[idx] = true; // rising edge only
      state.held[idx] = down;
      break;
    }

    // Also raise the dedicated click signal — separate from Action::Select above, since
    // click-to-move consumers need to know specifically "the player clicked a point",
    // not just "Select fired" (which keyboard/gamepad confirm also raise, with no
    // click position attached).
    if (button == GLFW_MOUSE_BUTTON_LEFT && down)
      state.mouse_click_pressed = true;
  }

  void poll_joystick(corundum::input::InputState &state, JoystickAxisState &axis) noexcept {
    constexpr int k_joy = GLFW_JOYSTICK_1;
    if (!glfwJoystickPresent(k_joy))
      return;

    // Buttons. Several actions here (Select, Cancel, Quit) are also keyboard-bound —
    // only release the shared `held` bit if THIS joystick button was the one that most
    // recently drove it, mirroring the axis handling below. Unconditionally writing
    // `state.held[idx] = down` here would clobber a concurrent keyboard press of the
    // same action back to false every poll (e.g. pressing Q to quit would immediately
    // un-set itself as soon as a gamepad is connected, since R1/button 7 isn't held).
    int button_count = 0;
    const unsigned char *buttons = glfwGetJoystickButtons(k_joy, &button_count);
    if (buttons) {
      for (const auto &[button, act] : k_button_bindings) {
        if (button >= button_count)
          continue;
        const auto idx = static_cast<std::size_t>(act);
        const bool down = (buttons[button] == GLFW_PRESS);
        if (down) {
          if (!state.held[idx])
            state.pressed[idx] = true;
          state.held[idx] = true;
        } else if (axis.button_active[idx]) {
          state.held[idx] = false;
        }
        axis.button_active[idx] = down;
      }
    }

    // Axes (left stick: axis 0=X, axis 1=Y; D-pad: axis 6=X, axis 7=Y on many controllers)
    int axis_count = 0;
    const float *axes = glfwGetJoystickAxes(k_joy, &axis_count);
    if (!axes)
      return;

    const float lx = axis_count > 0 ? axes[0] : 0.f;
    const float ly = axis_count > 1 ? axes[1] : 0.f;
    const float dpx = axis_count > 6 ? axes[6] : 0.f;
    const float dpy = axis_count > 7 ? axes[7] : 0.f;

    using Act = corundum::input::Action;
    constexpr std::array<Act, 4> k_axis_actions{Act::MoveUp, Act::MoveDown, Act::MoveLeft, Act::MoveRight};

    auto is_active = [ly, lx, dpx, dpy](Act act) noexcept -> bool {
      switch (act) {
      case Act::MoveUp:
        return ly < -k_axis_deadzone || dpy > k_axis_deadzone;
      case Act::MoveDown:
        return ly > k_axis_deadzone || dpy < -k_axis_deadzone;
      case Act::MoveLeft:
        return lx < -k_axis_deadzone || dpx < -k_axis_deadzone;
      case Act::MoveRight:
        return lx > k_axis_deadzone || dpx > k_axis_deadzone;
      default:
        return false;
      }
    };

    for (const Act act : k_axis_actions) {
      const auto idx = static_cast<std::size_t>(act);
      const bool active = is_active(act);
      if (active && !axis.axis_active[idx]) {
        if (!state.held[idx])
          state.pressed[idx] = true;
        state.held[idx] = true;
      } else if (!active && axis.axis_active[idx]) {
        state.held[idx] = false;
      }
      axis.axis_active[idx] = active;
    }
  }

} // namespace corundum::platform::glfw
