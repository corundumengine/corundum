// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/gameplay/codex/codex.hpp>
#include <corundum/gameplay/codex/loader.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/world/flags.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace codex = corundum::gameplay::codex;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_codex_", tag};
  }

} // namespace

TEST_CASE("codex loader: parses entries and preserves category and body") {
  const auto dir = temp_dir("load");
  const auto path = dir / "greyhollow.json";
  write_file(path, R"({
    "schema_version": 1,
    "entries": [
      { "id": "village", "title": "Greyhollow", "category": "Places", "body": "A frontier village." },
      { "id": "ember", "title": "The Hearth-Ember", "body": "A warm stone." }
    ]
  })");

  const auto result = codex::load_codex_file(path);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 2);
  CHECK((*result)[0].id == "village");
  CHECK((*result)[0].title == "Greyhollow");
  CHECK((*result)[0].category == "Places");
  CHECK((*result)[0].body == "A frontier village.");
  // An entry with no category loads with an empty one; the UI groups it under "Lore".
  CHECK((*result)[1].category.empty());
}

TEST_CASE("codex loader: schema failures are reported") {
  const auto dir = temp_dir("errors");
  write_file(dir / "bad_version.json", R"({"schema_version":2,"entries":[]})");
  write_file(dir / "missing_title.json", R"({"schema_version":1,"entries":[{"id":"x"}]})");
  write_file(dir / "no_entries.json", R"({"schema_version":1})");

  CHECK_FALSE(codex::load_codex_file(dir / "bad_version.json").has_value());
  CHECK_FALSE(codex::load_codex_file(dir / "missing_title.json").has_value());
  CHECK_FALSE(codex::load_codex_file(dir / "no_entries.json").has_value());
}

TEST_CASE("codex registry: loads a directory and skips duplicates") {
  const auto dir = temp_dir("registry");
  write_file(dir / "a.json", R"({
    "schema_version": 1,
    "entries": [ { "id": "a", "title": "A" }, { "id": "shared", "title": "First" } ]
  })");
  write_file(dir / "b.json", R"({
    "schema_version": 1,
    "entries": [ { "id": "b", "title": "B" }, { "id": "shared", "title": "Second" } ]
  })");

  codex::Registry registry;
  CHECK(registry.load_all(dir) == 3);
  REQUIRE(registry.find("shared") != nullptr);
  CHECK(registry.find("shared")->title == "First");
}

TEST_CASE("build_codex_entries: only unlocked entries, ordered by (category, title)") {
  codex::Registry registry;
  registry.add(codex::CodexEntry{.category = "Places", .id = "z", .title = "Zeta"});
  registry.add(codex::CodexEntry{.category = "Places", .id = "a", .title = "Alpha"});
  registry.add(codex::CodexEntry{.category = "Lore", .id = "m", .title = "Mu"});
  registry.add(codex::CodexEntry{.id = "locked", .title = "Hidden"});

  corundum::world::FlagStore flags;
  corundum::world::set_flag(flags, codex::flag_key("z"));
  corundum::world::set_flag(flags, codex::flag_key("a"));
  corundum::world::set_flag(flags, codex::flag_key("m"));

  const std::vector<codex::CodexEntry> entries = corundum::gameplay::screens::build_codex_entries(registry, flags);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].title == "Mu");    // category "Lore" sorts before "Places"
  CHECK(entries[1].title == "Alpha"); // then title order within Places
  CHECK(entries[2].title == "Zeta");
}

TEST_CASE("refresh_codex: rebuilds only while dirty") {
  codex::Registry registry;
  registry.add(codex::CodexEntry{.id = "a", .title = "A"});

  corundum::world::FlagStore flags;
  corundum::gameplay::screens::CodexState state{};
  corundum::gameplay::screens::refresh_codex(state, registry, flags);
  CHECK(state.entries.empty());
  CHECK_FALSE(state.dirty);

  // An unlock without marking dirty is deliberately not observed — the cache stands.
  corundum::world::set_flag(flags, codex::flag_key("a"));
  corundum::gameplay::screens::refresh_codex(state, registry, flags);
  CHECK(state.entries.empty());

  corundum::gameplay::screens::codex_mark_dirty(state);
  corundum::gameplay::screens::refresh_codex(state, registry, flags);
  REQUIRE(state.entries.size() == 1);
  CHECK(state.entries[0].title == "A");
}
