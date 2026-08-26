#include <doctest/doctest.h>

#include <corundum/core/files.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_files" / tag;
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
  }

} // namespace

TEST_CASE("list_dir_entries returns empty on missing dir") {
  const auto result = corundum::core::list_dir_entries("/nonexistent/path/__crpg__");
  CHECK(result.empty());
}

TEST_CASE("list_dir_entries returns empty on a file path") {
  const auto dir = temp_dir("not_a_dir");
  const auto f = dir / "a.txt";
  write_file(f, "hi");
  const auto result = corundum::core::list_dir_entries(f);
  CHECK(result.empty());
}

TEST_CASE("list_dir_entries returns directories first, then files alphabetically") {
  const auto dir = temp_dir("sorted");
  write_file(dir / "zebra.json", "{}");
  write_file(dir / "alpha.json", "{}");
  write_file(dir / "middle.txt", "x");
  fs::create_directory(dir / "zoo");
  fs::create_directory(dir / "beta");

  const auto result = corundum::core::list_dir_entries(dir);
  REQUIRE(result.size() == 5);

  // Directories first
  CHECK(result[0].is_dir);
  CHECK(result[1].is_dir);
  CHECK(!result[2].is_dir);
  CHECK(!result[3].is_dir);
  CHECK(!result[4].is_dir);

  // Directories sorted alphabetically
  CHECK(result[0].name == "beta");
  CHECK(result[1].name == "zoo");

  // Files sorted alphabetically
  CHECK(result[2].name == "alpha.json");
  CHECK(result[3].name == "middle.txt");
  CHECK(result[4].name == "zebra.json");

  // Paths are full and have correct filename
  CHECK(result[2].path.filename() == "alpha.json");
  CHECK(result[2].path == dir / "alpha.json");
}

TEST_CASE("list_dir_entries on empty dir returns empty") {
  const auto dir = temp_dir("empty");
  const auto result = corundum::core::list_dir_entries(dir);
  CHECK(result.empty());
}

TEST_CASE("has_extension matches case-insensitively without leading dot") {
  CHECK(corundum::core::has_extension("/foo/bar.JSON", "json"));
  CHECK(corundum::core::has_extension("/foo/bar.json", "JSON"));
  CHECK(corundum::core::has_extension("/foo/bar.Json", "json"));
  CHECK(corundum::core::has_extension("/foo/bar.json", "JSON"));
  CHECK(corundum::core::has_extension("bar.txt", "txt"));
  CHECK(corundum::core::has_extension("noext", "json") == false);
  CHECK(corundum::core::has_extension("foo.json", "txt") == false);
  CHECK(corundum::core::has_extension("foo.json", "jsonx") == false);
  // Path with no extension at all
  CHECK(corundum::core::has_extension("Makefile", "json") == false);
  // Empty ext + no extension: both sides are "" so they compare equal.
  CHECK(corundum::core::has_extension("Makefile", ""));
}
