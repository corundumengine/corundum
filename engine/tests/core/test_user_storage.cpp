// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_user_storage.hpp"
#include "temp_dir.hpp"

#include <corundum/core/json_io.hpp>
#include <corundum/core/user_storage.hpp>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
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

TEST_CASE("DirectoryUserStorage: a missing nested entry's error spells the path with native separators") {
  const auto dir = temp_dir("missing_nested");
  const corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.read("saves/slot_01.json");
  REQUIRE_FALSE(result.has_value());
  // Joined one component at a time, so on Windows every separator is a backslash.
  CHECK_EQ(result.error(), "cannot open: " + (dir.path() / "saves" / "slot_01.json").string());
}

TEST_CASE("DirectoryUserStorage: contains is false for a missing entry") {
  const auto dir = temp_dir("contains");
  const corundum::core::DirectoryUserStorage storage(dir.path());

  CHECK_FALSE(storage.contains("nope.json"));
}

TEST_CASE("DirectoryUserStorage: write rejects a name with a '..' component") {
  const auto dir = temp_dir("parent_escape");
  corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.write("../escape.txt", "x");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(),
           "invalid storage name '../escape.txt' (must be non-empty, relative, with no '..' component)");
  CHECK_FALSE(std::filesystem::exists(dir.path().parent_path() / "escape.txt"));
}

TEST_CASE("DirectoryUserStorage: write rejects an empty name") {
  const auto dir = temp_dir("empty_name");
  corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.write("", "x");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), "invalid storage name '' (must be non-empty, relative, with no '..' component)");
}

TEST_CASE("DirectoryUserStorage: write rejects an absolute name") {
  const auto dir = temp_dir("absolute_name");
  corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.write("/tmp/escape.txt", "x");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(),
           "invalid storage name '/tmp/escape.txt' (must be non-empty, relative, with no '..' component)");
}

#ifdef _WIN32
TEST_CASE("DirectoryUserStorage: write rejects a name with a Windows drive or UNC host") {
  const auto dir = temp_dir("windows_root_name");
  corundum::core::DirectoryUserStorage storage(dir.path());

  // Each name has a root name, which operator/ would put in place of the storage root's.
  // "C:escape.txt" is relative, so a check for an absolute path alone would accept it.
  const auto drive_relative = storage.write("C:escape.txt", "x");
  REQUIRE_FALSE(drive_relative.has_value());
  CHECK_EQ(drive_relative.error(),
           "invalid storage name 'C:escape.txt' (must be non-empty, relative, with no '..' component)");

  const auto drive_absolute = storage.write("C:\\escape.txt", "x");
  REQUIRE_FALSE(drive_absolute.has_value());
  CHECK_EQ(drive_absolute.error(),
           "invalid storage name 'C:\\escape.txt' (must be non-empty, relative, with no '..' component)");

  const auto unc = storage.write("\\\\localhost\\share\\escape.txt", "x");
  REQUIRE_FALSE(unc.has_value());
  CHECK_EQ(unc.error(),
           "invalid storage name '\\\\localhost\\share\\escape.txt' (must be non-empty, relative, with no '..' "
           "component)");
}
#endif

TEST_CASE("DirectoryUserStorage: write rejects a bare '..' name") {
  const auto dir = temp_dir("bare_parent");
  corundum::core::DirectoryUserStorage storage(dir.path());

  const auto result = storage.write("..", "x");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), "invalid storage name '..' (must be non-empty, relative, with no '..' component)");
}

TEST_CASE("DirectoryUserStorage: read and contains reject a '..' component") {
  const auto dir = temp_dir("read_parent_escape");
  const corundum::core::DirectoryUserStorage storage(dir.path());

  CHECK_FALSE(storage.contains("../escape.txt"));
  const auto result = storage.read("../escape.txt");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(),
           "invalid storage name '../escape.txt' (must be non-empty, relative, with no '..' component)");
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
