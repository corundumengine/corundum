// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/credits/loader.hpp>

#include "temp_dir.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

namespace fs = std::filesystem;
namespace credits = corundum::gameplay::credits;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_credits_", tag};
  }

} // namespace

TEST_CASE("credits loader: parses sections with headings and lines") {
  const auto dir = temp_dir("load");
  const auto path = dir / "credits.json";
  write_file(path, R"({
    "sections": [
      { "heading": "Design", "lines": ["Ada", "Grace"] },
      { "heading": "Engine", "lines": ["Corundum"] }
    ]
  })");

  const auto result = credits::load_credits_file(path);
  REQUIRE(result.has_value());
  REQUIRE(result->sections.size() == 2);
  CHECK(result->sections[0].heading == "Design");
  REQUIRE(result->sections[0].lines.size() == 2);
  CHECK(result->sections[0].lines[0] == "Ada");
  CHECK(result->sections[0].lines[1] == "Grace");
  CHECK(result->sections[1].heading == "Engine");
  CHECK(result->sections[1].lines[0] == "Corundum");
}

TEST_CASE("credits loader: an empty sections array is valid") {
  const auto dir = temp_dir("empty");
  const auto path = dir / "credits.json";
  write_file(path, R"({"sections": []})");

  const auto result = credits::load_credits_file(path);
  REQUIRE(result.has_value());
  CHECK(result->sections.empty());
}

TEST_CASE("credits loader: title is optional and parses when present") {
  const auto dir = temp_dir("title");

  const auto with_title = dir / "with_title.json";
  write_file(with_title, R"({"title": "Project X", "sections": []})");
  const auto r1 = credits::load_credits_file(with_title);
  REQUIRE(r1.has_value());
  CHECK(r1->title == "Project X");

  const auto without_title = dir / "without_title.json";
  write_file(without_title, R"({"sections": []})");
  const auto r2 = credits::load_credits_file(without_title);
  REQUIRE(r2.has_value());
  CHECK(r2->title.empty());
}

TEST_CASE("credits loader: a missing or malformed file is reported, not fatal") {
  const auto dir = temp_dir("errors");
  const auto missing = dir / "missing.json";
  CHECK_FALSE(credits::load_credits_file(missing).has_value());

  const auto malformed = dir / "malformed.json";
  write_file(malformed, "not json");
  CHECK_FALSE(credits::load_credits_file(malformed).has_value());

  const auto no_sections = dir / "no_sections.json";
  write_file(no_sections, R"({"other": []})");
  CHECK_FALSE(credits::load_credits_file(no_sections).has_value());

  const auto bad_line = dir / "bad_line.json";
  write_file(bad_line, R"({"sections": [{"heading": "x", "lines": [1]}]})");
  CHECK_FALSE(credits::load_credits_file(bad_line).has_value());

  const auto bad_title = dir / "bad_title.json";
  write_file(bad_title, R"({"title": 1, "sections": []})");
  CHECK_FALSE(credits::load_credits_file(bad_title).has_value());
}
