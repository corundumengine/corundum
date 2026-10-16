// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_user_storage.hpp"
#include "temp_dir.hpp"

#include <corundum/core/json_io.hpp>
#include <corundum/core/user_storage.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

using nlohmann::json;

namespace {

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_user_storage_", tag};
  }

} // namespace

TEST_CASE("DirectoryUserStorage: write then read round-trips the text") {
  const auto dir = temp_dir("round_trip");
  corundum::core::DirectoryUserStorage storage(dir.path());

  REQUIRE(storage.write("a.json", "hello").has_value());
  const auto result = storage.read("a.json");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, "hello");
}

TEST_CASE("DirectoryUserStorage: write creates intermediate folders") {
  const auto dir = temp_dir("nested");
  corundum::core::DirectoryUserStorage storage(dir.path());

  REQUIRE(storage.write("saves/slot_01.json", "data").has_value());
  CHECK(storage.contains("saves/slot_01.json"));
}

TEST_CASE("DirectoryUserStorage: write replaces an existing entry") {
  const auto dir = temp_dir("replace");
  corundum::core::DirectoryUserStorage storage(dir.path());

  REQUIRE(storage.write("a.json", "first").has_value());
  REQUIRE(storage.write("a.json", "second").has_value());
  const auto result = storage.read("a.json");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, "second");
}

TEST_CASE("DirectoryUserStorage: read of a missing entry reports an error") {
  const auto dir = temp_dir("missing");
  const corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.read("nope.json");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("cannot open") != std::string::npos);
}

TEST_CASE("DirectoryUserStorage: contains is false for a missing entry") {
  const auto dir = temp_dir("contains");
  const corundum::core::DirectoryUserStorage storage(dir.path());

  CHECK_FALSE(storage.contains("nope.json"));
}

TEST_CASE("make_user_storage: an empty game_id is an error") {
  const auto result = corundum::core::make_user_storage("");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("empty") != std::string::npos);
}

TEST_CASE("write_json(storage): output matches format_json") {
  corundum::test::MemoryUserStorage storage;
  const json j{{"b", 1}, {"a", 2}};

  REQUIRE(corundum::core::write_json(storage, "a.json", j).has_value());
  CHECK_EQ(storage.entries.at("a.json"), corundum::core::format_json(j));
}

TEST_CASE("DirectoryUserStorage: write_json then read_json round-trips the document") {
  const auto dir = temp_dir("json_round_trip");
  corundum::core::DirectoryUserStorage storage(dir.path());
  const json j{{"nested", {{"a", 1}, {"b", true}}}};

  REQUIRE(corundum::core::write_json(storage, "a.json", j).has_value());
  const auto result = corundum::core::read_json(storage, "a.json");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, j);
}

TEST_CASE("read_json(storage): malformed text names the entry") {
  corundum::test::MemoryUserStorage storage;
  storage.entries["slot.json"] = "{ not valid json";

  const auto result = corundum::core::read_json(storage, "slot.json", "save");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("malformed save in slot.json") != std::string::npos);
}
