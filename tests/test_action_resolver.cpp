// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>

#include <cstddef>

namespace {

  using corundum::input::Action;
  using corundum::input::ActionResolver;
  using corundum::input::InputState;

  /// Arbitrary distinct source indices; the resolver treats them as opaque.
  constexpr std::size_t k_source_primary = 0;
  constexpr std::size_t k_source_secondary = 1;
  constexpr std::size_t k_source_tertiary = 2;

} // namespace

TEST_CASE("ActionResolver — a source going down raises held and pressed") {
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);

  CHECK(state.is_held(Action::MoveUp));
  CHECK(state.is_pressed(Action::MoveUp));
}

TEST_CASE("ActionResolver — releasing the last source clears held") {
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);
  resolver.update(k_source_primary, Action::MoveUp, false, state);

  CHECK_FALSE(state.is_held(Action::MoveUp));
}

TEST_CASE("ActionResolver — releasing one of two sources keeps the action held") {
  // W and Up are both bound to MoveUp; releasing either must not stop movement.
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);
  resolver.update(k_source_secondary, Action::MoveUp, true, state);
  resolver.update(k_source_primary, Action::MoveUp, false, state);

  CHECK(state.is_held(Action::MoveUp));
}

TEST_CASE("ActionResolver — a second source pressed while held is not a new press") {
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);
  corundum::input::clear_pressed(state);
  resolver.update(k_source_secondary, Action::MoveUp, true, state);

  CHECK_FALSE(state.is_pressed(Action::MoveUp));
}

TEST_CASE("ActionResolver — repeated updates of an unchanged source do not re-raise pressed") {
  // Joystick sources are polled every frame, not event-driven.
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_tertiary, Action::MoveUp, true, state);
  corundum::input::clear_pressed(state);
  resolver.update(k_source_tertiary, Action::MoveUp, true, state);

  CHECK_FALSE(state.is_pressed(Action::MoveUp));
  CHECK(state.is_held(Action::MoveUp));
}

TEST_CASE("ActionResolver — pressing again after full release raises pressed") {
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);
  resolver.update(k_source_primary, Action::MoveUp, false, state);
  corundum::input::clear_pressed(state);
  resolver.update(k_source_primary, Action::MoveUp, true, state);

  CHECK(state.is_pressed(Action::MoveUp));
}

TEST_CASE("ActionResolver — releasing a keyboard tap does not clobber a held gamepad source") {
  // The reported bug: gamepad A held, keyboard Enter tapped. Enter's release used
  // to clear Select and then re-raise a spurious press on the next poll.
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_tertiary, Action::Select, true, state);
  corundum::input::clear_pressed(state);

  resolver.update(k_source_primary, Action::Select, true, state);
  resolver.update(k_source_primary, Action::Select, false, state);

  CHECK(state.is_held(Action::Select));
  CHECK_FALSE(state.is_pressed(Action::Select));
}

TEST_CASE("ActionResolver — sources are scoped to their own action") {
  InputState state{};
  ActionResolver resolver{};

  resolver.update(k_source_primary, Action::MoveUp, true, state);

  CHECK_FALSE(state.is_held(Action::MoveDown));
  CHECK_FALSE(state.is_held(Action::Select));

  // Releasing an inactive source for a different action must not disturb MoveUp.
  resolver.update(k_source_secondary, Action::MoveDown, false, state);
  CHECK(state.is_held(Action::MoveUp));
}
