// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>

#include <bitset>
#include <expected>
#include <optional>
#include <string>

namespace corundum::input {

  /** @brief Turns physical input into Action state through a runtime binding table.
   *
   *  The platform backend reports what the player physically did (keys, mouse buttons, cursor,
   *  scroll, the active gamepad's state) between begin_poll() and end_poll(). This class maps that
   *  input to Actions through its Bindings, where each row is one ActionResolver source. It also
   *  tracks the last device the player pressed, and can capture the next press for rebinding.
   *
   *  @note Not thread-safe; drive it from the main thread only.
   */
  class InputMapper {
  public:
    /** @brief Starts with default_bindings(). */
    InputMapper();

    [[nodiscard]] const Bindings &bindings() const noexcept {
      return bindings_;
    }

    /** @brief Replace the binding table.
     *
     *  Every action is released first, so nothing held under the old table stays latched.
     *
     *  @return An error, leaving the table unchanged, when @p bindings exceeds k_max_input_sources rows.
     */
    [[nodiscard]] std::expected<void, std::string> set_bindings(Bindings bindings);

    /** @brief Start a poll: clear the previous poll's presses, click and scroll; held state carries over. */
    void begin_poll() noexcept;

    /** @brief A key went down or up. The backend drops OS key repeats before calling this. */
    void key(Key key, bool down) noexcept;

    /** @brief A mouse button went down or up. A left press also raises InputState::mouse_click_pressed. */
    void mouse_button(MouseButton button, bool down) noexcept;

    /** @brief Cursor position in window coordinates. */
    void cursor(float x, float y) noexcept;

    /** @brief Scroll wheel movement; accumulates within one poll. */
    void scroll(float delta_y) noexcept;

    /** @brief The active gamepad's state this poll; its controls' up/down transitions drive their rows. */
    void gamepad(const GamepadState &state) noexcept;

    /** @brief No gamepad this poll: release every gamepad control, so a disconnect latches nothing. */
    void gamepad_absent() noexcept;

    /** @brief Finish the poll by accumulating it into @p destination (see input::accumulate_input). */
    void end_poll(InputState &destination) const noexcept;

    /** @brief Capture the next press instead of routing it to an action.
     *
     *  Presses made while capturing drive no action and raise no click. The first one ends the
     *  capture and becomes available from take_captured().
     */
    void begin_capture() noexcept;

    /** @brief Stop capturing without recording anything. */
    void cancel_capture() noexcept;

    [[nodiscard]] bool is_capturing() const noexcept {
      return capturing_;
    }

    /** @brief The captured press, once; nullopt while still capturing or when nothing was captured. */
    [[nodiscard]] std::optional<PhysicalInput> take_captured() noexcept;

    /** @brief Device of the most recent press (not release); Keyboard before any press. */
    [[nodiscard]] InputDevice last_device() const noexcept {
      return last_device_;
    }

    /** @brief True from a gamepad() poll until the next gamepad_absent(). */
    [[nodiscard]] bool gamepad_connected() const noexcept {
      return gamepad_connected_;
    }

  private:
    /** @brief Route one physical transition: record the device, satisfy a capture, or drive every matching row. */
    void apply(PhysicalInput input, bool down) noexcept;

    Bindings bindings_;

    std::optional<PhysicalInput> captured_;

    bool capturing_{false};

    std::bitset<k_gamepad_control_count> gamepad_down_{};

    bool gamepad_connected_{false};

    InputDevice last_device_{InputDevice::Keyboard};

    InputState poll_state_{};

    ActionResolver resolver_{};
  };

} // namespace corundum::input
