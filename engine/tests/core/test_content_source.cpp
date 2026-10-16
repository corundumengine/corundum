// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

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

  void write_bytes(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_content_", tag};
  }

} // namespace

TEST_CASE("DirectoryContentSource: read returns the file's exact bytes") {
  const auto dir = temp_dir("read_bytes");
  const std::vector<std::uint8_t> bytes = {0x00, 0x7B, 0xFF, 0x0A};
  write_bytes(dir / "raw.bin", bytes);

  const corundum::core::DirectoryContentSource source;
  const auto result = source.read(dir / "raw.bin");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, bytes);
}

TEST_CASE("DirectoryContentSource: read of a missing file names the path") {
  const corundum::core::DirectoryContentSource source;
  const auto result = source.read("/nonexistent/__corundum__/missing.json");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("/nonexistent/__corundum__/missing.json") != std::string::npos);
  CHECK(result.error().find("cannot open") != std::string::npos);
}

TEST_CASE("DirectoryContentSource: contains is true for a file, false for a missing path and a directory") {
  const auto dir = temp_dir("contains");
  write_bytes(dir / "a.bin", {0x01});
  fs::create_directories(dir / "sub");

  const corundum::core::DirectoryContentSource source;
  CHECK(source.contains(dir / "a.bin"));
  CHECK_FALSE(source.contains(dir / "missing.bin"));
  CHECK_FALSE(source.contains(dir / "sub"));
}

TEST_CASE("DirectoryContentSource: an empty root resolves paths as given") {
  const auto dir = temp_dir("empty_root");
  write_bytes(dir / "a.bin", {0x2A});

  const corundum::core::DirectoryContentSource source;
  const auto result = source.read(dir / "a.bin");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, std::vector<std::uint8_t>{0x2A});
}

TEST_CASE("DirectoryContentSource: a root prefixes relative paths") {
  const auto dir = temp_dir("root_prefix");
  write_bytes(dir / "a" / "b.json", {'{', '}'});

  const corundum::core::DirectoryContentSource source(dir.path());
  const auto result = source.read("a/b.json");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, std::vector<std::uint8_t>{'{', '}'});
}

TEST_CASE("DirectoryContentSource: list under a root returns paths read() accepts") {
  const auto dir = temp_dir("root_list");
  write_bytes(dir / "a" / "one.json", {'1'});

  const corundum::core::DirectoryContentSource source(dir.path());
  const auto entries = source.list("a", corundum::core::ListOptions{.extensions = {"json"}});
  REQUIRE(entries.has_value());
  REQUIRE(entries->size() == 1);
  CHECK_EQ((*entries)[0].name, "one.json");

  const auto bytes = source.read((*entries)[0].path);
  REQUIRE(bytes.has_value());
  CHECK_EQ(*bytes, std::vector<std::uint8_t>{'1'});
}

TEST_CASE("DirectoryContentSource: an absolute path bypasses the root") {
  const auto root = temp_dir("root_bypass");
  const auto other = temp_dir("other_bypass");
  write_bytes(other / "outside.bin", {0x55});

  const corundum::core::DirectoryContentSource source(root.path());
  const auto result = source.read(other / "outside.bin");
  REQUIRE(result.has_value());
  CHECK_EQ(*result, std::vector<std::uint8_t>{0x55});
}

TEST_CASE("DirectoryContentSource: list of a missing directory reports the error") {
  const corundum::core::DirectoryContentSource source;
  const auto result = source.list("/nonexistent/__corundum__/nope", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("/nonexistent/__corundum__/nope") != std::string::npos);
}
