// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "temp_dir.hpp"
#include <corundum/core/json_io.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using nlohmann::json;

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &path, std::string_view content) {
    std::ofstream f(path);
    f << content;
  }

  std::string read_file(const fs::path &path) {
    std::ifstream f(path);
    return std::string{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_json_io_", tag};
  }

} // namespace

TEST_CASE("write_json — sorts object keys recursively with 2-space indent and a trailing newline") {
  const auto dir = temp_dir("sorted_object");
  const auto path = dir / "sorted.json";

  json j;
  j["b"] = 1;
  j["a"]["y"] = 3;
  j["a"]["z"] = 2;

  REQUIRE(corundum::core::write_json(path, j).has_value());

  const std::string expected = "{\n"
                               "  \"a\": {\n"
                               "    \"y\": 3,\n"
                               "    \"z\": 2\n"
                               "  },\n"
                               "  \"b\": 1\n"
                               "}\n";
  CHECK(read_file(path) == expected);
}

TEST_CASE("write_json — keeps array order but sorts keys inside each element") {
  const auto dir = temp_dir("sorted_array");
  const auto path = dir / "array.json";

  json first;
  first["b"] = 2;
  first["a"] = 1;
  json second;
  second["d"] = 4;
  second["c"] = 3;
  json j = json::array();
  j.push_back(first);
  j.push_back(second);

  REQUIRE(corundum::core::write_json(path, j).has_value());

  const std::string content = read_file(path);
  const auto a = content.find("\"a\"");
  const auto b = content.find("\"b\"");
  const auto c = content.find("\"c\"");
  const auto d = content.find("\"d\"");
  REQUIRE(a != std::string::npos);
  REQUIRE(d != std::string::npos);
  CHECK(a < b);
  CHECK(b < c);
  CHECK(c < d);
}

TEST_CASE("read_json — accepts // and /* */ comments") {
  const auto dir = temp_dir("comments");
  const auto path = dir / "comments.json";
  write_file(path, R"({
    // line comment
    "a": 1, /* block comment */
    "b": 2
  })");

  auto result = corundum::core::read_json(path);
  REQUIRE(result.has_value());
  CHECK((*result)["a"] == 1);
  CHECK((*result)["b"] == 2);
}

TEST_CASE("read_json — round-trips a value written by write_json") {
  const auto dir = temp_dir("roundtrip");
  const auto path = dir / "roundtrip.json";

  json j;
  j["nested"]["flag"] = true;
  j["n"] = 3.5;
  j["items"] = json::array({1, 2, 3});

  REQUIRE(corundum::core::write_json(path, j).has_value());
  auto result = corundum::core::read_json(path);
  REQUIRE(result.has_value());
  CHECK(*result == j);
}

TEST_CASE("read_json — reports a missing file and malformed JSON") {
  const auto dir = temp_dir("read_errors");

  SUBCASE("missing file") {
    auto result = corundum::core::read_json(dir / "does_not_exist.json");
    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("cannot open") != std::string::npos);
  }

  SUBCASE("malformed JSON") {
    const auto path = dir / "malformed.json";
    write_file(path, "{ not valid json");
    auto result = corundum::core::read_json(path);
    CHECK_FALSE(result.has_value());
    CHECK(result.error().find("malformed JSON") != std::string::npos);
  }
}

TEST_CASE("read_json — a label names the document in both the missing-file and malformed errors") {
  const auto dir = temp_dir("read_labelled_errors");

  SUBCASE("missing file") {
    const auto path = dir / "missing_item.json";
    auto result = corundum::core::read_json(path, "item JSON");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("cannot open item JSON") != std::string::npos);
    CHECK(result.error().find(path.string()) != std::string::npos);
  }

  SUBCASE("malformed file") {
    const auto path = dir / "bad_item.json";
    write_file(path, "{ not valid json");
    auto result = corundum::core::read_json(path, "item JSON");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("malformed item JSON") != std::string::npos);
    CHECK(result.error().find(path.string()) != std::string::npos);
  }
}

TEST_CASE("write_json — reports an unopenable path") {
  const auto dir = temp_dir("write_error");
  auto result = corundum::core::write_json(dir / "missing_subdir" / "out.json", json::object());
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("cannot open") != std::string::npos);
}

TEST_CASE("write_json — replaces an existing file and leaves no temp file behind") {
  const auto dir = temp_dir("replace");
  const auto path = dir / "out.json";
  write_file(path, "old contents");

  json j;
  j["a"] = 1;
  REQUIRE(corundum::core::write_json(path, j).has_value());

  CHECK(read_file(path) == "{\n  \"a\": 1\n}\n");
  CHECK_FALSE(fs::exists(dir / "out.json.tmp"));
}

TEST_CASE("write_json — a failed write leaves the existing file untouched") {
  const auto dir = temp_dir("keep_on_failure");
  const auto path = dir / "out.json";
  write_file(path, "old contents");
  fs::create_directories(dir / "out.json.tmp"); // a directory where the temp file must go makes the open fail

  CHECK_FALSE(corundum::core::write_json(path, json::object()).has_value());
  CHECK(read_file(path) == "old contents");
}
