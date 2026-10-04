// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/world/ui_stack.hpp>

#include <cstddef>
#include <cstdint>

namespace {

  using corundum::world::GameMode;
  using corundum::world::UIStack;
  namespace screens = corundum::gameplay::screens;

} // namespace

TEST_CASE("UIStack: an empty stack reports Exploring") {
  const UIStack stack;
  CHECK(stack.empty());
  CHECK(stack.size() == 0);
  CHECK(stack.top() == GameMode::Exploring);
  CHECK_FALSE(stack.contains(screens::Journal));
}

TEST_CASE("UIStack: push opens a screen and pop peels one layer") {
  UIStack stack;
  stack.push(screens::Journal);
  CHECK(stack.top() == screens::Journal);
  CHECK(stack.contains(screens::Journal));
  CHECK(stack.size() == 1);

  stack.push(screens::Inventory);
  CHECK(stack.top() == screens::Inventory);
  CHECK(stack.contains(screens::Journal));
  CHECK(stack.size() == 2);

  stack.pop();
  CHECK(stack.top() == screens::Journal);

  stack.pop();
  CHECK(stack.top() == GameMode::Exploring);
  CHECK(stack.empty());
}

TEST_CASE("UIStack: popping an empty stack is a no-op") {
  UIStack stack;
  stack.pop();
  CHECK(stack.top() == GameMode::Exploring);
  CHECK(stack.empty());
}

TEST_CASE("UIStack: pushing Exploring is ignored — it is the empty-stack base") {
  UIStack stack;
  stack.push(GameMode::Exploring);
  CHECK(stack.empty());
  CHECK(stack.top() == GameMode::Exploring);
}

TEST_CASE("UIStack: clear returns to the base in one step") {
  UIStack stack;
  stack.push(GameMode::Prompt);
  stack.push(screens::Journal);
  stack.clear();
  CHECK(stack.empty());
  CHECK(stack.top() == GameMode::Exploring);
}

TEST_CASE("UIStack: depth is capped at k_max_depth") {
  UIStack stack;
  for (std::size_t i = 0; i < UIStack::k_max_depth + 3; ++i)
    stack.push(screens::Journal);
  CHECK(stack.size() == UIStack::k_max_depth);
}

TEST_CASE("UIStack: extension modes stack and resolve like engine modes") {
  // An extension mode is just a GameMode value at or above k_first_extension_mode; the stack
  // must not treat it specially.
  UIStack stack;
  CHECK(static_cast<std::uint8_t>(screens::Dialogue) >= corundum::world::k_first_extension_mode);

  stack.push(GameMode::Menu);
  stack.push(screens::Dialogue);
  CHECK(stack.top() == screens::Dialogue);
  CHECK(stack.contains(GameMode::Menu));
  CHECK(stack.contains(screens::Dialogue));

  // A journal opened over a dialogue peels back to the dialogue, not to Exploring.
  stack.push(screens::Journal);
  stack.pop();
  CHECK(stack.top() == screens::Dialogue);

  stack.pop();
  CHECK(stack.top() == GameMode::Menu);

  stack.pop();
  CHECK(stack.top() == GameMode::Exploring);
}
