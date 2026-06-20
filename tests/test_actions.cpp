#include <doctest/doctest.h>

#include <corundum/input/actions.hpp>

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
