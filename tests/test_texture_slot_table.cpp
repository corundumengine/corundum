// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/texture_slot_table.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

using corundum::platform::TextureSlotTable;

namespace {

  /// Stands in for a backend's handle set; the table only stores and returns it.
  struct FakeHandles {
    uint32_t image{0};

    uint32_t view{0};
  };

  using Table = TextureSlotTable<FakeHandles>;
  using Slot = Table::Slot;

  /// Unwrap a value the case has already established is present. Returning by value keeps
  /// the helper safe to call on a temporary; the REQUIRE aborts the case when the value is
  /// empty, which the analyzer cannot see through the doctest macro.
  template <typename T> T require_value(const std::optional<T> &value) {
    REQUIRE(value.has_value());

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access) — the REQUIRE above aborts the case when empty
    return value.value();
  }

} // namespace

TEST_CASE("TextureSlotTable: adopt publishes 1-based ids and reports each slot's extent") {
  Table table;

  CHECK(table.adopt({.image = 10, .view = 11}, 4, 8) == 1);
  CHECK(table.adopt({.image = 20, .view = 21}, 16, 32) == 2);
  CHECK(table.slot_count() == 2);

  const Slot first = require_value(table.find(1));
  CHECK(first.width == 4);
  CHECK(first.height == 8);
  CHECK(first.payload.image == 10);
  CHECK(first.payload.view == 11);

  const Slot second = require_value(table.find(2));
  CHECK(second.width == 16);
  CHECK(second.height == 32);
}

TEST_CASE("TextureSlotTable: release hands back the retired handles") {
  Table table;
  table.adopt({.image = 10, .view = 11}, 4, 8);

  const FakeHandles released = require_value(table.release(1));

  CHECK(released.image == 10);
  CHECK(released.view == 11);
}

TEST_CASE("TextureSlotTable: a released id goes to the next texture without growing the table") {
  Table table;
  table.adopt({.image = 10, .view = 11}, 4, 8);
  table.adopt({.image = 20, .view = 21}, 4, 8);

  CHECK(table.release(1).has_value());
  CHECK(table.adopt({.image = 30, .view = 31}, 4, 8) == 1);
  CHECK(table.slot_count() == 2);
  CHECK(require_value(table.find(1)).payload.image == 30);
  CHECK_FALSE(table.find(3).has_value());
}

TEST_CASE("TextureSlotTable: the sentinel, out-of-range, and already-retired ids are never live") {
  Table table;
  table.adopt({.image = 10, .view = 11}, 4, 8);

  CHECK_FALSE(table.release(0).has_value());
  CHECK_FALSE(table.release(2).has_value());
  CHECK_FALSE(table.find(0).has_value());
  CHECK_FALSE(table.find(2).has_value());

  CHECK(table.release(1).has_value());
  CHECK_FALSE(table.release(1).has_value());
  CHECK_FALSE(table.find(1).has_value());
}

TEST_CASE("TextureSlotTable: retiring every id leaves the whole table reusable") {
  Table table;
  table.adopt({.image = 10, .view = 11}, 4, 8);
  table.adopt({.image = 20, .view = 21}, 4, 8);

  int retired = 0;
  for (uint32_t id = 1; id <= table.slot_count(); ++id) {
    if (table.release(id))
      ++retired;
  }

  CHECK(retired == 2);
  CHECK(table.adopt({.image = 30, .view = 31}, 4, 8) == 1);
  CHECK(table.adopt({.image = 40, .view = 41}, 4, 8) == 2);
}

TEST_CASE("TextureSlotTable: an empty table has no ids to retire") {
  Table table;

  CHECK(table.slot_count() == 0);
  CHECK_FALSE(table.release(1).has_value());
  CHECK_FALSE(table.find(1).has_value());
}

TEST_CASE("TextureSlotTable: slot_count is the high-water mark, not the live count") {
  Table table;
  table.adopt({.image = 10, .view = 11}, 4, 8);
  table.adopt({.image = 20, .view = 21}, 4, 8);
  table.adopt({.image = 30, .view = 31}, 4, 8);

  CHECK(table.release(1).has_value());
  CHECK(table.release(2).has_value());

  CHECK(table.slot_count() == 3);
  CHECK(table.find(3).has_value());
}
