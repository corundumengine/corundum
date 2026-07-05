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

    constexpr std::array<KeyBinding, 14> k_default_bindings{{
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
        {GLFW_KEY_EQUAL, Action::ZoomIn}, // '=' doubles as '+' without needing Shift
        {GLFW_KEY_MINUS, Action::ZoomOut},
    }};

    struct ButtonBinding {
      int button;
      Action action;
    };

    // GLFW_GAMEPAD_BUTTON_* indices into GLFWgamepadstate::buttons — these are SDL
    // gamepad-DB-mapped positions (A/B/X/Y, bumpers, Start, ...), not raw HID report
    // indices, so they stay correct across different controllers/platforms. Raw
    // glfwGetJoystickButtons() indices vary by device (e.g. one controller reported Y at
    // raw index 4 and R1 at raw index 7 — nothing like the assumed Xbox layout), so
    // binding directly to those was unreliable.
    // Zoom is bound to the analog triggers (L2/R2), not the bumpers — those are reported
    // as axes, not buttons, in the mapped gamepad API, so they're handled alongside the
    // stick/d-pad axis logic in poll_joystick(), not in this table.
    constexpr std::array<ButtonBinding, 3> k_button_bindings{{
        {GLFW_GAMEPAD_BUTTON_A, Action::Select},
        {GLFW_GAMEPAD_BUTTON_B, Action::Cancel},
        {GLFW_GAMEPAD_BUTTON_START, Action::Quit},
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

    // GLFW_GAMEPAD_AXIS_LEFT_TRIGGER/RIGHT_TRIGGER rest at -1.0 (released) and read +1.0
    // fully pressed; 0.0 is roughly a half-press, used as the "activated" threshold.
    inline constexpr float k_trigger_threshold = 0.f;

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

  void translate_scroll(double yoffset, corundum::input::InputState &state) noexcept {
    state.scroll_delta_y += static_cast<float>(yoffset);
  }

  void poll_joystick(corundum::input::InputState &state, JoystickAxisState &axis) noexcept {
    constexpr int k_joy = GLFW_JOYSTICK_1;
    if (!glfwJoystickPresent(k_joy))
      return;

    // Only mapped gamepads (an SDL gamepad-DB entry exists for this device) are
    // supported — that mapping is exactly what makes GLFW_GAMEPAD_BUTTON_* portable
    // across controllers. An unmapped joystick has no reliable button semantics to bind.
    if (!glfwJoystickIsGamepad(k_joy))
      return;

    GLFWgamepadstate gp;
    if (!glfwGetGamepadState(k_joy, &gp))
      return;

    // Buttons. Several actions here (Select, Cancel, Quit) are also keyboard-bound —
    // only release the shared `held` bit if THIS joystick button was the one that most
    // recently drove it, mirroring the axis handling below. Unconditionally writing
    // `state.held[idx] = down` here would clobber a concurrent keyboard press of the
    // same action back to false every poll (e.g. pressing Q to quit would immediately
    // un-set itself as soon as a gamepad is connected, since Start isn't held).
    for (const auto &[button, act] : k_button_bindings) {
      const auto idx = static_cast<std::size_t>(act);
      const bool down = (gp.buttons[button] == GLFW_PRESS);
      if (down) {
        if (!state.held[idx])
          state.pressed[idx] = true;
        state.held[idx] = true;
      } else if (axis.button_active[idx]) {
        state.held[idx] = false;
      }
      axis.button_active[idx] = down;
    }

    // Left stick + D-pad (D-pad is reported as digital buttons in the mapped gamepad
    // API, not axes — no deadzone needed for it).
    const float lx = gp.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    const float ly = gp.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    const bool dpad_up = gp.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS;
    const bool dpad_down = gp.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS;
    const bool dpad_left = gp.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS;
    const bool dpad_right = gp.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS;
    const float lt = gp.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
    const float rt = gp.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];

    using Act = corundum::input::Action;
    constexpr std::array<Act, 6> k_axis_actions{Act::MoveUp,    Act::MoveDown, Act::MoveLeft,
                                                Act::MoveRight, Act::ZoomIn,   Act::ZoomOut};

    auto is_active = [ly, lx, dpad_up, dpad_down, dpad_left, dpad_right, lt, rt](Act act) noexcept -> bool {
      switch (act) {
      case Act::MoveUp:
        return ly < -k_axis_deadzone || dpad_up;
      case Act::MoveDown:
        return ly > k_axis_deadzone || dpad_down;
      case Act::MoveLeft:
        return lx < -k_axis_deadzone || dpad_left;
      case Act::MoveRight:
        return lx > k_axis_deadzone || dpad_right;
      case Act::ZoomIn:
        return rt > k_trigger_threshold; // R2
      case Act::ZoomOut:
        return lt > k_trigger_threshold; // L2
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
