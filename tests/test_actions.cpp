#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>

using corundum::input::accumulate_input;
using corundum::input::Action;
using corundum::input::clear_pressed;
using corundum::input::k_action_count;
using corundum::input::pressed_actions;

// ── InputState::is_held / is_pressed ─────────────────────────────────────────

TEST_CASE("is_held — returns true after setting held bit") {
  corundum::input::InputState state{};
  state.held.set(static_cast<std::size_t>(Action::MoveUp));
  CHECK(state.is_held(Action::MoveUp));
}

TEST_CASE("is_held — returns false for unset bit") {
  corundum::input::InputState state{};
  CHECK_FALSE(state.is_held(Action::Select));
}

TEST_CASE("is_pressed — returns true after setting pressed bit") {
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::Quit));
  CHECK(state.is_pressed(Action::Quit));
}

TEST_CASE("is_pressed — returns false for unset bit") {
  corundum::input::InputState state{};
  CHECK_FALSE(state.is_pressed(Action::Cancel));
}

// ── clear_pressed ────────────────────────────────────────────────────────────

TEST_CASE("clear_pressed — resets all pressed flags") {
  corundum::input::InputState state{};
  for (std::size_t i = 0; i < k_action_count; ++i) {
    state.pressed.set(i);
  }
  clear_pressed(state);
  for (std::size_t i = 0; i < k_action_count; ++i) {
    CHECK_FALSE(state.is_pressed(static_cast<Action>(i)));
  }
}

TEST_CASE("clear_pressed — does not affect held flags") {
  corundum::input::InputState state{};
  state.held.set(static_cast<std::size_t>(Action::MoveUp));
  state.pressed.set(static_cast<std::size_t>(Action::MoveUp));
  clear_pressed(state);
  CHECK(state.is_held(Action::MoveUp));
}

// ── pressed_actions ──────────────────────────────────────────────────────────

TEST_CASE("pressed_actions — returns empty when no actions pressed") {
  corundum::input::InputState state{};
  const auto result = pressed_actions(state);
  CHECK(result.empty());
  CHECK(result.size() == 0);
}

TEST_CASE("pressed_actions — returns single pressed action") {
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::MoveLeft));
  const auto result = pressed_actions(state);
  CHECK(result.size() == 1);
  CHECK(result.actions[0] == Action::MoveLeft);
}

TEST_CASE("pressed_actions — returns multiple pressed actions") {
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::MoveUp));
  state.pressed.set(static_cast<std::size_t>(Action::Select));
  state.pressed.set(static_cast<std::size_t>(Action::Quit));
  const auto result = pressed_actions(state);
  CHECK(result.size() == 3);
  CHECK(result.actions[0] == Action::MoveUp);
  CHECK(result.actions[1] == Action::Select);
  CHECK(result.actions[2] == Action::Quit);
}

TEST_CASE("pressed_actions — ignores held-only actions") {
  corundum::input::InputState state{};
  state.held.set(static_cast<std::size_t>(Action::MoveDown));
  const auto result = pressed_actions(state);
  CHECK(result.empty());
}

// ── mouse_click_pressed / Action::Select separation ─────────────────────────────

TEST_CASE("clear_pressed — also resets mouse_click_pressed") {
  corundum::input::InputState state{};
  state.mouse_click_pressed = true;
  clear_pressed(state);
  CHECK_FALSE(state.mouse_click_pressed);
}

TEST_CASE("accumulate_input — ORs mouse_click_pressed rather than overwriting") {
  corundum::input::InputState dst{};
  dst.mouse_click_pressed = true; // e.g. already raised earlier this fixed-step tick
  corundum::input::InputState src{};
  src.mouse_click_pressed = false;
  accumulate_input(dst, src);
  CHECK(dst.mouse_click_pressed); // must not be clobbered back to false
}

TEST_CASE("accumulate_input — a keyboard/gamepad Select press does not raise mouse_click_pressed") {
  // Regression for the bug where Action::Select (shared by mouse click and keyboard/
  // gamepad confirm) was used to gate click-to-move: pressing Enter to confirm dialogue
  // must not look like a click.
  corundum::input::InputState dst{};
  corundum::input::InputState src{};
  src.pressed.set(static_cast<std::size_t>(Action::Select)); // keyboard Enter, not a click
  accumulate_input(dst, src);
  CHECK(dst.is_pressed(Action::Select));
  CHECK_FALSE(dst.mouse_click_pressed);
}

TEST_CASE("pressed_actions — range-for iteration works") {
  corundum::input::InputState state{};
  state.pressed.set(static_cast<std::size_t>(Action::Cancel));
  state.pressed.set(static_cast<std::size_t>(Action::Select));

  std::size_t count{};
  for (const auto action : pressed_actions(state)) {
    (void)action;
    ++count;
  }
  CHECK(count == 2);
}
