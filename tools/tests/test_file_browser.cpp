// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/toolkit/widgets/file_browser.hpp>
#include <corundum/toolkit/widgets/text_buffer.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <random>
#include <string_view>
#include <system_error>

using corundum::toolkit::widgets::confirm_path;
using corundum::toolkit::widgets::copy_to_buffer;
using corundum::toolkit::widgets::FileBrowserState;
using corundum::toolkit::widgets::navigate_to;
using corundum::toolkit::widgets::open_browser;
using corundum::toolkit::widgets::OpenRequest;
using corundum::toolkit::widgets::SaveRequest;

namespace {

  /// A throwaway directory holding sub/, a.json, b.txt, c.JSON and sub/inner.json, removed on exit.
  class ScratchTree {
  public:
    ScratchTree() {
      static int counter = 0;
      root_ = std::filesystem::temp_directory_path() /
              std::format("corundum_file_browser_{}_{}", std::random_device{}(), counter++);
      std::filesystem::create_directories(root_ / "sub");
      write(root_ / "a.json", "{}");
      write(root_ / "b.txt", "plain");
      write(root_ / "c.JSON", "{}");
      write(root_ / "sub" / "inner.json", "{}");
    }

    ~ScratchTree() {
      std::error_code ignored;
      std::filesystem::remove_all(root_, ignored);
    }

    ScratchTree(const ScratchTree &) = delete;
    ScratchTree &operator=(const ScratchTree &) = delete;
    ScratchTree(ScratchTree &&) = delete;
    ScratchTree &operator=(ScratchTree &&) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    static void write(const std::filesystem::path &path, std::string_view contents) {
      std::ofstream out(path);
      REQUIRE(out.good());
      out << contents;
    }

    std::filesystem::path root_;
  };

  void set_name(FileBrowserState &fb, std::string_view name) {
    copy_to_buffer(fb.name_buf.data(), fb.name_buf.size(), name);
  }

  [[nodiscard]] std::optional<std::size_t> find_entry(const FileBrowserState &fb, std::string_view name) {
    for (std::size_t index = 0; index < fb.entries.size(); ++index)
      if (fb.entries[index].name == name)
        return index;
    return std::nullopt;
  }

} // namespace

TEST_CASE("File browser: an Open request lists directories plus matching files, directories first") {
  const ScratchTree tree;
  FileBrowserState fb;

  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});

  REQUIRE(fb.entries.size() == 3);
  CHECK(fb.entries[0].is_dir);
  CHECK(fb.entries[0].name == "sub");
  CHECK(fb.entries[1].name == "a.json");
  CHECK(fb.entries[2].name == "c.JSON");
}

TEST_CASE("File browser: an Open request with no extensions lists every file") {
  const ScratchTree tree;
  FileBrowserState fb;

  open_browser(fb, "Open", tree.root(), OpenRequest{});

  REQUIRE(fb.entries.size() == 4);
  CHECK(find_entry(fb, "b.txt").has_value());
}

TEST_CASE("File browser: a Save request lists every file and pre-fills the default name") {
  const ScratchTree tree;
  FileBrowserState fb;

  open_browser(fb, "Save", tree.root(), SaveRequest{.default_name = "hero.json", .extension = "json"});

  CHECK(std::string_view{fb.name_buf.data()} == "hero.json");
  REQUIRE(fb.entries.size() == 4);
  CHECK(find_entry(fb, "b.txt").has_value());
}

TEST_CASE("File browser: navigating in Open mode drops the selection and the name it mirrored") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});
  fb.selected_entry = find_entry(fb, "a.json");
  set_name(fb, "a.json");

  navigate_to(fb, tree.root() / "sub");

  CHECK_FALSE(fb.selected_entry.has_value());
  CHECK(std::string_view{fb.name_buf.data()}.empty());
  REQUIRE(fb.entries.size() == 1);
  CHECK(fb.entries[0].name == "inner.json");
}

TEST_CASE("File browser: navigating in Save mode keeps the typed name") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Save", tree.root(), SaveRequest{.default_name = "hero.json", .extension = "json"});

  navigate_to(fb, tree.root() / "sub");

  CHECK(std::string_view{fb.name_buf.data()} == "hero.json");
}

TEST_CASE("File browser: confirming an Open browser returns the highlighted file") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});

  fb.selected_entry = find_entry(fb, "c.JSON");

  CHECK(confirm_path(fb) == tree.root() / "c.JSON");
}

TEST_CASE("File browser: confirming an Open browser with a directory highlighted yields nothing") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});

  fb.selected_entry = find_entry(fb, "sub");

  CHECK_FALSE(confirm_path(fb).has_value());
}

TEST_CASE("File browser: confirming an Open browser with nothing highlighted yields nothing") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});

  CHECK_FALSE(fb.selected_entry.has_value());
  CHECK_FALSE(confirm_path(fb).has_value());
}

TEST_CASE("File browser: confirming a Save browser appends the request extension to a bare name") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Save", tree.root(), SaveRequest{.default_name = "", .extension = "json"});
  set_name(fb, "hero");

  CHECK(confirm_path(fb) == tree.root() / "hero.json");
}

TEST_CASE("File browser: confirming a Save browser leaves a name that already has the extension") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Save", tree.root(), SaveRequest{.default_name = "", .extension = "json"});
  set_name(fb, "hero.JSON");

  CHECK(confirm_path(fb) == tree.root() / "hero.JSON");
}

TEST_CASE("File browser: confirming a Save browser with an empty name yields nothing") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Save", tree.root(), SaveRequest{.default_name = "", .extension = "json"});

  CHECK(std::string_view{fb.name_buf.data()}.empty());
  CHECK_FALSE(confirm_path(fb).has_value());
}

TEST_CASE("File browser: confirming a closed browser yields nothing") {
  const FileBrowserState fb;

  CHECK_FALSE(confirm_path(fb).has_value());
}

TEST_CASE("File browser: opening on a path that isn't a directory falls back to the working directory") {
  const ScratchTree tree;
  FileBrowserState fb;

  open_browser(fb, "Open", tree.root() / "a.json" / "missing", OpenRequest{.extensions = {"json"}});

  CHECK(fb.current_dir == std::filesystem::current_path());
}

TEST_CASE("File browser: a directory that can't be listed leaves an empty listing and stays open") {
  const ScratchTree tree;
  FileBrowserState fb;
  open_browser(fb, "Open", tree.root(), OpenRequest{.extensions = {"json"}});

  navigate_to(fb, tree.root() / "missing");

  CHECK(fb.entries.empty());
  CHECK_FALSE(fb.selected_entry.has_value());
  CHECK(fb.request.has_value());
  CHECK_FALSE(confirm_path(fb).has_value());
}
