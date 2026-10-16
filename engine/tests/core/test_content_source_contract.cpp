// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_content_source.hpp"
#include "temp_dir.hpp"

#include <corundum/core/content_source.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

  void write_text(const fs::path &path, std::string_view contents) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path);
    f << contents;
  }

  std::vector<std::string> names_of(const std::vector<corundum::core::DirEntry> &entries) {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const corundum::core::DirEntry &entry : entries)
      names.push_back(entry.name);
    return names;
  }

  // One flat list of contract assertions; splitting it would hide the shared contract.
  // NOLINTNEXTLINE(readability-function-size)
  void check_contract(const corundum::core::ContentSource &source, std::string_view label) {
    INFO(label);

    const auto one = source.read("a/one.json");
    REQUIRE(one.has_value());
    CHECK_EQ(*one, std::vector<std::uint8_t>{'1'});

    CHECK(source.contains("a/sub/three.json"));
    CHECK_FALSE(source.contains("a/missing.json"));

    const auto filtered = source.list("a", corundum::core::ListOptions{.extensions = {"json"}});
    REQUIRE(filtered.has_value());
    CHECK_EQ(names_of(*filtered), (std::vector<std::string>{"sub", "one.json"}));

    const auto recursive = source.list("a", corundum::core::ListOptions{.extensions = {"json"}, .recursive = true});
    REQUIRE(recursive.has_value());
    bool found_three = false;
    for (const corundum::core::DirEntry &entry : *recursive)
      found_three = found_three || entry.name == "three.json";
    CHECK(found_three);

    // The directory has files, but the filter removes them all: still a success with no entries.
    const auto empty = source.list("a/sub", corundum::core::ListOptions{.extensions = {"txt"}});
    REQUIRE(empty.has_value());
    CHECK(empty->empty());

    CHECK_FALSE(source.list("nope", {}).has_value());
  }

} // namespace

TEST_CASE("ContentSource contract: DirectoryContentSource") {
  const auto dir = corundum::test::TempDir{"crpg_test_content_contract_", "disk"};
  write_text(dir / "a" / "one.json", "1");
  write_text(dir / "a" / "two.txt", "2");
  write_text(dir / "a" / "sub" / "three.json", "3");

  const corundum::core::DirectoryContentSource source(dir.path());
  check_contract(source, "DirectoryContentSource");
}

TEST_CASE("ContentSource contract: MemoryContentSource") {
  corundum::test::MemoryContentSource source;
  source.add("a/one.json", "1");
  source.add("a/two.txt", "2");
  source.add("a/sub/three.json", "3");

  check_contract(source, "MemoryContentSource");
}

TEST_CASE("DirectoryContentSource: list of an empty directory succeeds with no entries") {
  const auto dir = corundum::test::TempDir{"crpg_test_content_contract_", "empty_dir"};
  fs::create_directories(dir / "empty");

  const corundum::core::DirectoryContentSource source(dir.path());
  const auto result = source.list("empty", {});
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("MemoryContentSource: a directory with no files under it reads as missing") {
  corundum::test::MemoryContentSource source;
  source.add("a/one.json", "1");

  const auto result = source.list("a/empty", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("Directory does not exist") != std::string::npos);
}

TEST_CASE("MemoryContentSource: a trailing slash lists the same directory") {
  corundum::test::MemoryContentSource source;
  source.add("a/one.json", "1");
  source.add("a/sub/three.json", "3");

  const auto plain = source.list("a", {});
  const auto trailing = source.list("a/", {});
  REQUIRE(plain.has_value());
  REQUIRE(trailing.has_value());
  CHECK_EQ(names_of(*plain), names_of(*trailing));
}
