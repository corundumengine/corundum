// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "input_translator.hpp"

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>

#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace corundum::platform::glfw {

  namespace {

    using corundum::input::Action;
    using corundum::input::ActionResolver;
    using corundum::input::InputState;

    /// A GLFW key/gamepad-button code paired with the Action it drives.
    struct Binding {
      int code{};
      Action action{};
    };

    constexpr std::array<Binding, 17> k_key_bindings{
        {
            {.code = GLFW_KEY_W, .action = Action::MoveUp},
            {.code = GLFW_KEY_S, .action = Action::MoveDown},
            {.code = GLFW_KEY_A, .action = Action::MoveLeft},
            {.code = GLFW_KEY_D, .action = Action::MoveRight},
            {.code = GLFW_KEY_UP, .action = Action::MoveUp},
            {.code = GLFW_KEY_DOWN, .action = Action::MoveDown},
            {.code = GLFW_KEY_LEFT, .action = Action::MoveLeft},
            {.code = GLFW_KEY_RIGHT, .action = Action::MoveRight},
            {.code = GLFW_KEY_ENTER, .action = Action::Select},
            {.code = GLFW_KEY_SPACE, .action = Action::Select},
            {.code = GLFW_KEY_ESCAPE, .action = Action::Cancel},
            {.code = GLFW_KEY_Q, .action = Action::Quit},
            {.code = GLFW_KEY_I, .action = Action::Inventory},
            {.code = GLFW_KEY_EQUAL, .action = Action::ZoomIn}, // '=' doubles as '+' without needing Shift
            {.code = GLFW_KEY_MINUS, .action = Action::ZoomOut},
            {.code = GLFW_KEY_F5, .action = Action::QuickSave},
            {.code = GLFW_KEY_F9, .action = Action::QuickLoad},
        },
    };

    // GLFW_GAMEPAD_BUTTON_* are SDL gamepad-DB-mapped positions (A/B/X/Y, bumpers,
    // Start, ...), not raw HID report indices, so they stay correct across different
    // controllers and platforms. Raw glfwGetJoystickButtons() indices vary by device
    // (one controller reported Y at raw index 4 and R1 at raw index 7), so binding
    // directly to those was unreliable.
    constexpr std::array<Binding, 3> k_gamepad_button_bindings{
        {
            {.code = GLFW_GAMEPAD_BUTTON_A, .action = Action::Select},
            {.code = GLFW_GAMEPAD_BUTTON_B, .action = Action::Cancel},
            {.code = GLFW_GAMEPAD_BUTTON_START, .action = Action::Quit},
        },
    };

    constexpr std::array<Binding, 1> k_mouse_bindings{
        {
            {.code = GLFW_MOUSE_BUTTON_LEFT, .action = Action::Select},
        },
    };

    // ── Source indices ──────────────────────────────────────────────────────────
    // Each binding gets a distinct stable index so the resolver can tell whose
    // release cleared an action. Keyboard, mouse, and gamepad occupy disjoint ranges.
    constexpr std::size_t k_key_source_base = 0;
    constexpr std::size_t k_mouse_source_base = k_key_source_base + k_key_bindings.size();
    constexpr std::size_t k_gamepad_button_source_base = k_mouse_source_base + k_mouse_bindings.size();
    constexpr std::size_t k_gamepad_axis_source_base = k_gamepad_button_source_base + k_gamepad_button_bindings.size();

    // The stick and D-pad are separate physical sources for the same four move
    // actions, so both fold into the resolver rather than being OR-ed ad hoc.
    enum class GamepadAxisSource : std::uint8_t {
      StickUp = static_cast<std::uint8_t>(k_gamepad_axis_source_base),
      StickDown,
      StickLeft,
      StickRight,
      DpadUp,
      DpadDown,
      DpadLeft,
      DpadRight,
      LeftTrigger,
      RightTrigger,
    };
    constexpr std::size_t k_gamepad_axis_source_count = 10;
    constexpr std::size_t k_window_close_source = k_gamepad_axis_source_base + k_gamepad_axis_source_count;

    static_assert(k_window_close_source < corundum::input::k_max_input_sources,
                  "input source indices must fit ActionResolver");

    constexpr Action action_for(GamepadAxisSource source) noexcept {
      switch (source) {
        case GamepadAxisSource::StickUp:
        case GamepadAxisSource::DpadUp:
          return Action::MoveUp;
        case GamepadAxisSource::StickDown:
        case GamepadAxisSource::DpadDown:
          return Action::MoveDown;
        case GamepadAxisSource::StickLeft:
        case GamepadAxisSource::DpadLeft:
          return Action::MoveLeft;
        case GamepadAxisSource::StickRight:
        case GamepadAxisSource::DpadRight:
          return Action::MoveRight;
        case GamepadAxisSource::LeftTrigger:
          return Action::ZoomOut; // L2
        case GamepadAxisSource::RightTrigger:
          return Action::ZoomIn; // R2
      }
      return Action::Count;
    }

    void update_gamepad_axis_source(GamepadAxisSource source, bool down, ActionResolver &resolver,
                                    InputState &state) noexcept {
      resolver.update(static_cast<std::size_t>(source), action_for(source), down, state);
    }

    /// Release every gamepad-owned source, so a disconnect or an unmapped device
    /// cannot leave an action's held bit latched.
    void release_gamepad_sources(ActionResolver &resolver, InputState &state) noexcept {
      for (std::size_t i = 0; i < k_gamepad_button_bindings.size(); ++i)
        resolver.update(k_gamepad_button_source_base + i, k_gamepad_button_bindings[i].action, false, state);

      for (std::size_t i = 0; i < k_gamepad_axis_source_count; ++i) {
        const auto source = static_cast<GamepadAxisSource>(k_gamepad_axis_source_base + i);
        update_gamepad_axis_source(source, false, resolver, state);
      }
    }

    /// Index of the first connected mapped gamepad, or -1 when there is none.
    /// Unmapped joysticks are skipped: only a mapped gamepad's GLFW_GAMEPAD_* layout
    /// is portable, so an unmapped device has no reliable button semantics to bind.
    int first_gamepad() noexcept {
      for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick) {
        if (glfwJoystickPresent(joystick) != 0 && glfwJoystickIsGamepad(joystick) != 0)
          return joystick;
      }
      return -1;
    }

    // Radial magnitude gate for the stick, plus a per-axis floor for direction.
    // A radial gate keeps the centre dead while still letting a gentle diagonal
    // register — a per-axis 0.5 threshold required ~0.7 on each axis before a
    // diagonal moved at all.
    inline constexpr float k_stick_deadzone = 0.5f;
    inline constexpr float k_stick_direction_threshold = 0.3f;

    // Gamepad triggers rest at -1.0 (released) and read +1.0 fully pressed, so 0.0
    // is roughly a half-press and serves as the "engaged" threshold.
    inline constexpr float k_trigger_threshold = 0.f;

  } // namespace

  void translate_key(int key, int action, ActionResolver &resolver, InputState &state) noexcept {
    if (action == GLFW_REPEAT)
      return;

    const bool down = (action == GLFW_PRESS);
    for (std::size_t i = 0; i < k_key_bindings.size(); ++i) {
      if (k_key_bindings[i].code != key)
        continue;
      resolver.update(k_key_source_base + i, k_key_bindings[i].action, down, state);
      return;
    }
  }

  void translate_mouse_button(int button, int action, ActionResolver &resolver, InputState &state) noexcept {
    const bool down = (action == GLFW_PRESS);

    for (std::size_t i = 0; i < k_mouse_bindings.size(); ++i) {
      if (k_mouse_bindings[i].code != button)
        continue;
      resolver.update(k_mouse_source_base + i, k_mouse_bindings[i].action, down, state);
      break;
    }

    // Also raise the dedicated click signal — separate from Action::Select, since
    // click-to-move consumers need to know specifically "the player clicked a point",
    // not just "Select fired" (which keyboard/gamepad confirm also raise, with no
    // click position attached).
    if (button == GLFW_MOUSE_BUTTON_LEFT && down)
      state.mouse_click_pressed = true;
  }

  void translate_scroll(double yoffset, InputState &state) noexcept {
    state.scroll_delta_y += static_cast<float>(yoffset);
  }

  void translate_window_close(ActionResolver &resolver, InputState &state) noexcept {
    resolver.update(k_window_close_source, Action::Quit, true, state);
  }

  void poll_gamepad(ActionResolver &resolver, InputState &state) noexcept {
    const int gamepad_index = first_gamepad();
    GLFWgamepadstate gamepad{};
    if (gamepad_index < 0 || glfwGetGamepadState(gamepad_index, &gamepad) == 0) {
      release_gamepad_sources(resolver, state);
      return;
    }

    for (std::size_t i = 0; i < k_gamepad_button_bindings.size(); ++i) {
      const bool down = gamepad.buttons[k_gamepad_button_bindings[i].code] == GLFW_PRESS;
      resolver.update(k_gamepad_button_source_base + i, k_gamepad_button_bindings[i].action, down, state);
    }

    const float left_x = gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    const float left_y = gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    const bool stick_engaged = std::sqrt((left_x * left_x) + (left_y * left_y)) >= k_stick_deadzone;

    update_gamepad_axis_source(GamepadAxisSource::StickUp, stick_engaged && left_y <= -k_stick_direction_threshold,
                               resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::StickDown, stick_engaged && left_y >= k_stick_direction_threshold,
                               resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::StickLeft, stick_engaged && left_x <= -k_stick_direction_threshold,
                               resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::StickRight, stick_engaged && left_x >= k_stick_direction_threshold,
                               resolver, state);

    update_gamepad_axis_source(GamepadAxisSource::DpadUp, gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] == GLFW_PRESS,
                               resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::DpadDown,
                               gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] == GLFW_PRESS, resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::DpadLeft,
                               gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] == GLFW_PRESS, resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::DpadRight,
                               gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] == GLFW_PRESS, resolver, state);

    update_gamepad_axis_source(GamepadAxisSource::LeftTrigger,
                               gamepad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > k_trigger_threshold, resolver, state);
    update_gamepad_axis_source(GamepadAxisSource::RightTrigger,
                               gamepad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > k_trigger_threshold, resolver, state);
  }

} // namespace corundum::platform::glfw
