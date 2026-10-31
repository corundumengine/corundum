// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>

namespace {

  using corundum::world::FlagStore;

} // namespace

TEST_CASE("flags: visit_count reads only the characters a viewed key covers") {
  const FlagStore flags{{"quest.started", 3}, {"quest.started.late", 5}};
  const std::string text{"quest.started.late"};
  const std::string_view key = std::string_view{text}.substr(0, 13);

  CHECK_EQ(corundum::world::visit_count(flags, key), 3);
}

TEST_CASE("flags: has_flag finds a viewed key but not the longer string around it") {
  const FlagStore flags{{"door.opened", 1}};
  const std::string text{"door.opened.twice"};

  CHECK(corundum::world::has_flag(flags, std::string_view{text}.substr(0, 11)));
  CHECK_FALSE(corundum::world::has_flag(flags, text));
}

TEST_CASE("flags: set_flag through a view stores its own copy of the key") {
  FlagStore flags;
  std::string text{"lever.pulled"};

  corundum::world::set_flag(flags, std::string_view{text});
  text = "xxxxx.xxxxxx";

  const FlagStore expected{{"lever.pulled", 1}};
  CHECK_EQ(flags, expected);
}

TEST_CASE("flags: clear_flag through a view erases only that key") {
  FlagStore flags{{"lever.pulled", 2}, {"lever.broken", 1}};

  corundum::world::clear_flag(flags, std::string_view{"lever.pulled"});

  const FlagStore expected{{"lever.broken", 1}};
  CHECK_EQ(flags, expected);
}
