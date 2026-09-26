// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>
#include <corundum/input/input_mapper.hpp>
#include <corundum/input/input_system.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/window.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

  using corundum::input::Action;
  using corundum::input::InputMapper;
  using corundum::input::InputState;
  using corundum::input::Key;
  using corundum::platform::PlatformEvents;

  /// Window double that records polls and feeds scripted physical input through the mapper,
  /// mirroring how the GLFW backend reports physical input to it.
  class RecordingWindow final : public corundum::platform::Window {
  public:
    int poll_count{};
    float scripted_scroll{};
    std::vector<std::pair<Key, bool>> scripted_keys;
    PlatformEvents scripted_events{};

    [[nodiscard]] bool is_open() const override {
      return true;
    }

    void close() override {}

    void show() override {}

    void poll_game_input(InputMapper &mapper, PlatformEvents &events) override {
      ++poll_count;
      for (const auto &[key, down] : scripted_keys)
        mapper.key(key, down);
      scripted_keys.clear();

      if (scripted_scroll != 0.f) {
        mapper.scroll(scripted_scroll);
        scripted_scroll = 0.f;
      }

      merge_events(events, scripted_events);
      scripted_events = {};
    }

    [[nodiscard]] std::pair<int, int> size() const override {
      return {0, 0};
    }

    void set_vsync(bool /*enabled*/) override {}

    [[nodiscard]] std::string input_label(corundum::input::PhysicalInput input) const override {
      return std::string{corundum::input::name_of(input)};
    }

    [[nodiscard]] void *native_handle() const override {
      return nullptr;
    }
  };

} // namespace

TEST_CASE("poll — forwards to the window's poll_game_input once per call") {
  RecordingWindow window;
  InputMapper mapper;
  InputState state{};
  PlatformEvents events{};

  corundum::input::poll(mapper, state, window, events);
  CHECK(window.poll_count == 1);

  corundum::input::poll(mapper, state, window, events);
  CHECK(window.poll_count == 2);
}

TEST_CASE("poll — a press raised by the window reaches the caller's state") {
  RecordingWindow window;
  window.scripted_keys.emplace_back(Key::Enter, true);
  InputMapper mapper;
  InputState state{};
  PlatformEvents events{};

  corundum::input::poll(mapper, state, window, events);

  CHECK(state.is_pressed(Action::Select));
}

TEST_CASE("poll — does not clear presses already latched in the state") {
  // poll only accumulates; clearing is clear_pressed's job, called per simulation step.
  RecordingWindow window;
  InputMapper mapper;
  InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::MoveUp));
  PlatformEvents events{};

  corundum::input::poll(mapper, state, window, events);

  CHECK(state.is_pressed(Action::MoveUp));
}

TEST_CASE("poll — repeated polls OR presses rather than replacing them") {
  RecordingWindow window;
  InputMapper mapper;
  InputState state{};
  PlatformEvents events{};

  window.scripted_keys.emplace_back(Key::W, true);
  corundum::input::poll(mapper, state, window, events);
  window.scripted_keys.emplace_back(Key::Escape, true);
  corundum::input::poll(mapper, state, window, events);

  CHECK(state.is_pressed(Action::MoveUp));
  CHECK(state.is_pressed(Action::Cancel));
}

TEST_CASE("poll — overwrites held with the window's current key state") {
  RecordingWindow window;
  InputMapper mapper;
  InputState state{};
  PlatformEvents events{};

  window.scripted_keys = {{Key::W, true}, {Key::Enter, true}};
  corundum::input::poll(mapper, state, window, events);
  CHECK(state.is_held(Action::MoveUp));
  CHECK(state.is_held(Action::Select));

  window.scripted_keys.emplace_back(Key::W, false);
  corundum::input::poll(mapper, state, window, events);
  CHECK_FALSE(state.is_held(Action::MoveUp));
  CHECK(state.is_held(Action::Select));
}

TEST_CASE("poll — accumulates the scroll delta") {
  RecordingWindow window;
  InputMapper mapper;
  InputState state{};
  state.scroll_delta_y = 1.f;
  PlatformEvents events{};

  window.scripted_scroll = 2.f;
  corundum::input::poll(mapper, state, window, events);

  CHECK(state.scroll_delta_y == doctest::Approx(3.f));
}

TEST_CASE("poll — surfaces platform events queued by the window") {
  RecordingWindow window;
  window.scripted_events.focus_lost = true;
  InputMapper mapper;
  InputState state{};
  PlatformEvents events{};

  corundum::input::poll(mapper, state, window, events);

  CHECK(events.focus_lost);
  CHECK_FALSE(events.focus_gained);

  // Events are one-shot: the next poll reports none.
  PlatformEvents next{};
  corundum::input::poll(mapper, state, window, next);
  CHECK_FALSE(next.focus_lost);
}
