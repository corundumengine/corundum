// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/input_mapper.hpp>

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>

#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace corundum::input {

  InputMapper::InputMapper() : bindings_{default_bindings()} {}

  std::expected<void, std::string> InputMapper::set_bindings(Bindings bindings) {
    if (bindings.size() > k_max_input_sources)
      return std::unexpected(
          std::format("binding table has {} rows; the limit is {}", bindings.size(), k_max_input_sources));

    bindings_ = std::move(bindings);
    resolver_ = ActionResolver{};
    poll_state_.held.reset();
    return {};
  }

  void InputMapper::begin_poll() noexcept {
    clear_pressed(poll_state_);
  }

  void InputMapper::key(Key key, bool down) noexcept {
    apply(physical(key), down);
  }

  void InputMapper::mouse_button(MouseButton button, bool down) noexcept {
    const bool was_capturing{capturing_};
    apply(physical(button), down);
    if (button == MouseButton::Left && down && !was_capturing)
      poll_state_.mouse_click_pressed = true;
  }

  void InputMapper::cursor(float x, float y) noexcept {
    poll_state_.mouse_x = x;
    poll_state_.mouse_y = y;
  }

  void InputMapper::scroll(float delta_y) noexcept {
    poll_state_.scroll_delta_y += delta_y;
  }

  void InputMapper::gamepad(const GamepadState &state) noexcept {
    gamepad_connected_ = true;
    for (std::size_t i = 0; i < k_gamepad_control_count; ++i) {
      const auto control = static_cast<GamepadControl>(i);
      const bool now = is_down(state, control);
      if (now != gamepad_down_[i]) {
        apply(physical(control), now);
        gamepad_down_[i] = now;
      }
    }
  }

  void InputMapper::gamepad_absent() noexcept {
    for (std::size_t i = 0; i < k_gamepad_control_count; ++i) {
      if (gamepad_down_[i])
        apply(physical(static_cast<GamepadControl>(i)), false);
    }
    gamepad_down_.reset();
    gamepad_connected_ = false;
  }

  void InputMapper::end_poll(InputState &destination) const noexcept {
    // Latch this poll's starting position as the baseline for the next intent, before accumulate
    // overwrites it. A screen then only moves focus when the cursor actually moved.
    destination.prev_mouse_x = destination.mouse_x;
    destination.prev_mouse_y = destination.mouse_y;
    accumulate_input(destination, poll_state_);
  }

  void InputMapper::begin_capture() noexcept {
    capturing_ = true;
  }

  void InputMapper::cancel_capture() noexcept {
    capturing_ = false;
  }

  std::optional<PhysicalInput> InputMapper::take_captured() noexcept {
    return std::exchange(captured_, std::nullopt);
  }

  void InputMapper::apply(PhysicalInput input, bool down) noexcept {
    if (down)
      last_device_ = input.device;

    if (capturing_ && down) {
      captured_ = input;
      capturing_ = false;
      return;
    }

    for (std::size_t i = 0; i < bindings_.size(); ++i) {
      if (bindings_[i].input == input)
        resolver_.update(i, bindings_[i].action, down, poll_state_);
    }
  }

} // namespace corundum::input
