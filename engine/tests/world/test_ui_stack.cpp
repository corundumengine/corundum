// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/ui_stack.hpp>

#include <cstddef>

namespace {

  using corundum::world::GameMode;
  using corundum::world::UIStack;

} // namespace

TEST_CASE("UIStack: an empty stack reports Exploring") {
  const UIStack stack;
  CHECK(stack.empty());
  CHECK(stack.size() == 0);
  CHECK(stack.top() == GameMode::Exploring);
  CHECK_FALSE(stack.contains(GameMode::Journal));
}

TEST_CASE("UIStack: push opens a screen and pop peels one layer") {
  UIStack stack;
  stack.push(GameMode::Journal);
  CHECK(stack.top() == GameMode::Journal);
  CHECK(stack.contains(GameMode::Journal));
  CHECK(stack.size() == 1);

  stack.push(GameMode::Inventory);
  CHECK(stack.top() == GameMode::Inventory);
  CHECK(stack.contains(GameMode::Journal));
  CHECK(stack.size() == 2);

  stack.pop();
  CHECK(stack.top() == GameMode::Journal);

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
  stack.push(GameMode::Journal);
  stack.clear();
  CHECK(stack.empty());
  CHECK(stack.top() == GameMode::Exploring);
}

TEST_CASE("UIStack: depth is capped at k_max_depth") {
  UIStack stack;
  for (std::size_t i = 0; i < UIStack::k_max_depth + 3; ++i)
    stack.push(GameMode::Journal);
  CHECK(stack.size() == UIStack::k_max_depth);
}
