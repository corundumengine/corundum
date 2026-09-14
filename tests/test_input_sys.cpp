// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>
#include <corundum/input/input_sys.hpp>
#include <corundum/platform/window.hpp>

#include <cstddef>
#include <utility>

namespace {

  using corundum::input::accumulate_input;
  using corundum::input::Action;
  using corundum::input::InputState;

  /// Window double that records polls and surfaces a scripted InputState, mirroring how
  /// the GLFW backend accumulates its translated events into the caller's state.
  class RecordingWindow final : public corundum::platform::Window {
  public:
    int poll_count{};
    InputState scripted{};

    [[nodiscard]] bool is_open() const override {
      return true;
    }

    void close() override {}

    void poll_game_input(InputState &input) override {
      ++poll_count;
      accumulate_input(input, scripted);
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void resize(unsigned /*width*/, unsigned /*height*/) override {}

    [[nodiscard]] std::pair<int, int> size() const override {
      return {0, 0};
    }

    void set_vsync(bool /*enabled*/) override {}

    [[nodiscard]] void *native_handle() const override {
      return nullptr;
    }
  };

} // namespace

TEST_CASE("poll — forwards to the window's poll_game_input once per call") {
  RecordingWindow window;
  corundum::input::InputState state{};

  corundum::input::poll(state, window);
  CHECK(window.poll_count == 1);

  corundum::input::poll(state, window);
  CHECK(window.poll_count == 2);
}

TEST_CASE("poll — a press raised by the window reaches the caller's state") {
  RecordingWindow window;
  window.scripted.pressed.set(static_cast<std::size_t>(Action::Select));
  corundum::input::InputState state{};

  corundum::input::poll(state, window);

  CHECK(state.is_pressed(Action::Select));
}

TEST_CASE("poll — does not clear presses already latched in the state") {
  // poll only accumulates; clearing is clear_pressed's job, called per simulation step.
  RecordingWindow window;
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::MoveUp));

  corundum::input::poll(state, window);

  CHECK(state.is_pressed(Action::MoveUp));
}

TEST_CASE("poll — repeated polls OR presses rather than replacing them") {
  RecordingWindow window;
  corundum::input::InputState state{};

  window.scripted.pressed.set(static_cast<std::size_t>(Action::MoveUp));
  corundum::input::poll(state, window);
  window.scripted.pressed.set(static_cast<std::size_t>(Action::Cancel));
  corundum::input::poll(state, window);

  CHECK(state.is_pressed(Action::MoveUp));
  CHECK(state.is_pressed(Action::Cancel));
}

TEST_CASE("poll — overwrites held with the window's current key state") {
  RecordingWindow window;
  window.scripted.held.set(static_cast<std::size_t>(Action::Select));
  corundum::input::InputState state{};
  state.held.set(static_cast<std::size_t>(Action::MoveUp));

  corundum::input::poll(state, window);

  CHECK(state.is_held(Action::Select));
  CHECK_FALSE(state.is_held(Action::MoveUp));
}

TEST_CASE("poll — accumulates the mouse click and scroll delta") {
  RecordingWindow window;
  window.scripted.mouse_click_pressed = true;
  window.scripted.scroll_delta_y = 2.f;
  corundum::input::InputState state{};
  state.scroll_delta_y = 1.f;

  corundum::input::poll(state, window);

  CHECK(state.mouse_click_pressed);
  CHECK(state.scroll_delta_y == doctest::Approx(3.f));
}
