// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/window_mode.hpp>
#include <corundum/input/input_mapper.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/window.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace corundum::platform::null {

/** No-op Window for headless lifecycle tests.
 *
 *  `open_` toggles between `is_open()` and `close()`. Stores dimensions so `size()` matches what
 *  the test sets. `poll_game_input` feeds the mapper whatever a test has scripted (key transitions
 *  once; the gamepad and cursor every poll) and emits the queued `scripted_events`.
 */
class NullWindow final : public Window {
public:
  explicit NullWindow(unsigned width, unsigned height) : width_{width}, height_{height} {}

  [[nodiscard]] bool is_open() const override { return open_; }

  void close() override { open_ = false; }

  void show() override { visible = true; }

  void poll_game_input(input::InputMapper &mapper, PlatformEvents &events) override {
    for (const auto &[key, down] : scripted_keys)
      mapper.key(key, down);
    scripted_keys.clear();

    for (const auto &[button, down] : scripted_mouse)
      mapper.mouse_button(button, down);
    scripted_mouse.clear();

    if (gamepad)
      mapper.gamepad(*gamepad);
    else
      mapper.gamepad_absent();

    mapper.cursor(cursor.first, cursor.second);

    merge_events(events, scripted_events);
    scripted_events = {};
  }

  [[nodiscard]] core::math::IntSize size() const override {
    return {.width = static_cast<int>(width_), .height = static_cast<int>(height_)};
  }

  void set_vsync(bool /*enabled*/) override {}

  void set_window_mode(core::WindowMode mode) override { mode_ = mode; }

  [[nodiscard]] core::WindowMode window_mode() const override { return mode_; }

  [[nodiscard]] std::string input_label(input::PhysicalInput input) const override {
    return std::string{input::name_of(input)};
  }

  [[nodiscard]] void *native_handle() const override { return nullptr; }

  /** True once show() has been called; lets a test assert the engine revealed the window. */
  bool visible{false};

  /** Cursor position in window coordinates, reported on every poll like a real window's. */
  std::pair<float, float> cursor{};

  /** Connected gamepad state reported on every poll; nullopt means no gamepad. */
  std::optional<input::GamepadState> gamepad;

  /** Key transitions fed on the next poll_game_input(), then cleared. */
  std::vector<std::pair<input::Key, bool>> scripted_keys;

  /** Mouse-button transitions fed on the next poll_game_input(), then cleared. */
  std::vector<std::pair<input::MouseButton, bool>> scripted_mouse;

  /** Events emitted on the next poll_game_input(), then cleared.
   *
   *  Tests set these to script focus, quit, display and controller changes the engine would
   *  otherwise receive from a real host OS.
   */
  PlatformEvents scripted_events{};

private:
  bool open_ = true;
  unsigned width_;
  unsigned height_;
  core::WindowMode mode_{core::WindowMode::Windowed};
};

} // namespace corundum::platform::null
