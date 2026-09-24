// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/toolkit/editor/undo.hpp>

using corundum::toolkit::editor::UndoStack;

TEST_CASE("UndoStack — a fresh stack can neither undo nor redo") {
  UndoStack<int> stack;
  int out = -1;

  CHECK_FALSE(stack.can_undo());
  CHECK_FALSE(stack.can_redo());
  CHECK_FALSE(stack.undo(out));
  CHECK_FALSE(stack.redo(out));
}

TEST_CASE("UndoStack — undo and redo walk the pushed snapshots") {
  UndoStack<int> stack;
  stack.push(1);
  stack.push(2);
  stack.push(3);

  CHECK(stack.can_undo());
  CHECK_FALSE(stack.can_redo());

  int out = 0;
  REQUIRE(stack.undo(out));
  CHECK(out == 2);
  REQUIRE(stack.undo(out));
  CHECK(out == 1);
  CHECK_FALSE(stack.can_undo());

  REQUIRE(stack.redo(out));
  CHECK(out == 2);
  REQUIRE(stack.redo(out));
  CHECK(out == 3);
  CHECK_FALSE(stack.can_redo());
}

TEST_CASE("UndoStack — a push after undo discards the redo history") {
  UndoStack<int> stack;
  stack.push(1);
  stack.push(2);
  stack.push(3);

  int out = 0;
  REQUIRE(stack.undo(out)); // back to 2
  CHECK(out == 2);

  stack.push(9);
  CHECK_FALSE(stack.can_redo());

  REQUIRE(stack.undo(out));
  CHECK(out == 2);
  REQUIRE(stack.undo(out));
  CHECK(out == 1);
}

TEST_CASE("UndoStack — clear resets both directions") {
  UndoStack<int> stack;
  stack.push(1);
  stack.push(2);
  stack.clear();

  int out = 0;
  CHECK_FALSE(stack.can_undo());
  CHECK_FALSE(stack.can_redo());
  CHECK_FALSE(stack.undo(out));
}

TEST_CASE("UndoStack — depth cap drops the oldest snapshot, keeping the most recent 256") {
  UndoStack<int> stack;
  for (int i = 0; i < 257; ++i)
    stack.push(i);

  int out = -1;
  int undo_count = 0;
  while (stack.can_undo()) {
    REQUIRE(stack.undo(out));
    ++undo_count;
  }

  // 256 retained snapshots => 255 possible undo steps.
  CHECK(undo_count == 255);
  // The oldest surviving snapshot is 1; snapshot 0 was evicted by the cap.
  CHECK(out == 1);
}
