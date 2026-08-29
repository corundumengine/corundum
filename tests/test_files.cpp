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

TEST_CASE("list_dir_entries reports an error on a missing directory") {
  const auto result = corundum::core::list_dir_entries("/nonexistent/path/__crpg__");
  REQUIRE(!result.has_value());
  CHECK(result.error().find("/nonexistent/path/__crpg__") != std::string::npos);
}

TEST_CASE("list_dir_entries reports an error when path is a file") {
  const auto dir = temp_dir("not_a_dir");
  const auto f = dir / "a.txt";
  write_file(f, "hi");
  const auto result = corundum::core::list_dir_entries(f);
  REQUIRE(!result.has_value());
  CHECK(result.error().find("not a directory") != std::string::npos);
}

TEST_CASE("list_dir_entries returns directories first, then files alphabetically") {
  const auto dir = temp_dir("sorted");
  write_file(dir / "zebra.json", "{}");
  write_file(dir / "alpha.json", "{}");
  write_file(dir / "middle.txt", "x");
  fs::create_directory(dir / "zoo");
  fs::create_directory(dir / "beta");

  const auto result = corundum::core::list_dir_entries(dir);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 5);

  // Directories first
  CHECK((*result)[0].is_dir);
  CHECK((*result)[1].is_dir);
  CHECK(!(*result)[2].is_dir);
  CHECK(!(*result)[3].is_dir);
  CHECK(!(*result)[4].is_dir);

  // Directories sorted alphabetically
  CHECK((*result)[0].name == "beta");
  CHECK((*result)[1].name == "zoo");

  // Files sorted alphabetically
  CHECK((*result)[2].name == "alpha.json");
  CHECK((*result)[3].name == "middle.txt");
  CHECK((*result)[4].name == "zebra.json");

  // Paths are full and have correct filename
  CHECK((*result)[2].path.filename() == "alpha.json");
  CHECK((*result)[2].path == dir / "alpha.json");
}

TEST_CASE("list_dir_entries on empty dir returns an empty vector") {
  const auto dir = temp_dir("empty");
  const auto result = corundum::core::list_dir_entries(dir);
  REQUIRE(result.has_value());
  CHECK(result->empty());
}

TEST_CASE("list_dir_entries with extensions filter only keeps matching files") {
  const auto dir = temp_dir("ext_filter");
  write_file(dir / "a.json", "{}");
  write_file(dir / "b.txt", "x");
  write_file(dir / "c.JSON", "{}");
  fs::create_directory(dir / "subdir_json");
  write_file(dir / "subdir_json" / "nested.json", "{}");

  const auto result = corundum::core::list_dir_entries(dir, {{"json"}});
  REQUIRE(result.has_value());

  // Two matching files + one subdir, sorted dirs-first then alphabetical by name.
  REQUIRE(result->size() == 3);
  CHECK((*result)[0].is_dir);
  CHECK((*result)[0].name == "subdir_json");
  CHECK((*result)[1].name == "a.json");
  CHECK((*result)[2].name == "c.JSON");
}

TEST_CASE("list_dir_entries recursive walks subdirectories and applies the extension filter to files") {
  const auto dir = temp_dir("recursive");
  write_file(dir / "root.json", "{}");
  write_file(dir / "root.txt", "x");
  fs::create_directory(dir / "sub");
  write_file(dir / "sub" / "child.json", "{}");
  write_file(dir / "sub" / "child.bin", "x");
  fs::create_directory(dir / "sub" / "deep");
  write_file(dir / "sub" / "deep" / "leaf.json", "{}");

  const auto result = corundum::core::list_dir_entries(dir, {.extensions = {"json"}, .recursive = true});
  REQUIRE(result.has_value());

  // Five matching entries; sort is directories-first then alphabetical by filename component.
  // Names: "sub" (dir), "deep" (dir), "root.json", "child.json", "leaf.json"
  // → deep, sub, child.json, leaf.json, root.json
  REQUIRE(result->size() == 5);

  CHECK((*result)[0].is_dir);
  CHECK((*result)[0].name == "deep");
  CHECK((*result)[1].is_dir);
  CHECK((*result)[1].name == "sub");
  CHECK(!(*result)[2].is_dir);
  CHECK((*result)[2].name == "child.json");
  CHECK(!(*result)[3].is_dir);
  CHECK((*result)[3].name == "leaf.json");
  CHECK(!(*result)[4].is_dir);
  CHECK((*result)[4].name == "root.json");
}

TEST_CASE("list_dir_entries recursive without filter returns every entry") {
  const auto dir = temp_dir("recursive_all");
  write_file(dir / "a.txt", "x");
  fs::create_directory(dir / "d");
  write_file(dir / "d" / "b.json", "{}");

  const auto result = corundum::core::list_dir_entries(dir, {.recursive = true});
  REQUIRE(result.has_value());
  // One file at root + one subdir + one file inside the subdir = 3 entries.
  REQUIRE(result->size() == 3);
}

TEST_CASE("list_dir_entries rejects with error on the same path used twice in a row") {
  const auto dir = temp_dir("fresh");
  write_file(dir / "ok.txt", "x");
  const auto ok = corundum::core::list_dir_entries(dir);
  REQUIRE(ok.has_value());

  // After removing the directory, the second call surfaces the missing-dir error instead of
  // silently producing an empty vector — this is the core behavior change behind the API.
  fs::remove_all(dir);
  const auto gone = corundum::core::list_dir_entries(dir);
  REQUIRE(!gone.has_value());
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
