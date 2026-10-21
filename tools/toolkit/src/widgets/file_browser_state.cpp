// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <corundum/core/files.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <corundum/toolkit/widgets/text_buffer.hpp>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace corundum::toolkit::widgets {

  namespace {
    [[nodiscard]] bool matches_extensions(const std::filesystem::path &path, std::span<const std::string> extensions) {
      if (extensions.empty())
        return true;
      return std::ranges::any_of(
          extensions, [&path](const std::string &extension) { return corundum::core::has_extension(path, extension); });
    }

    [[nodiscard]] const OpenRequest *find_open_request(const FileBrowserState &fb) {
      if (!fb.request)
        return nullptr;
      return std::get_if<OpenRequest>(&*fb.request);
    }

    /// Append @p extension to @p name unless @p name already ends in it.
    [[nodiscard]] std::string with_extension(std::string_view name, std::string_view extension) {
      const bool already_has_extension = corundum::core::has_extension(std::filesystem::path{name}, extension);
      if (extension.empty() || already_has_extension)
        return std::string{name};
      std::string extended{name};
      extended += '.';
      extended += extension;
      return extended;
    }

    /// Rebuild @p fb's listing from current_dir: directories, plus in Open mode the files whose extension
    /// matches. list_dir_entries() already sorts directories first, then alphabetically.
    void refresh_entries(FileBrowserState &fb) {
      fb.entries.clear();
      fb.selected_entry.reset();
      const OpenRequest *open = find_open_request(fb);
      auto listed = corundum::core::list_dir_entries(fb.current_dir);
      if (!listed) {
        std::print(stderr, "file_browser: failed to list {}: {}\n", fb.current_dir.string(), listed.error());
        return;
      }
      for (auto &entry : *listed) {
        if (open != nullptr && !entry.is_dir && !matches_extensions(entry.path, open->extensions))
          continue;
        fb.entries.push_back(std::move(entry));
      }
    }
  } // namespace

  void open_browser(FileBrowserState &fb, std::string_view title, const std::filesystem::path &start_dir,
                    FileBrowserRequest request) {
    fb.request = std::move(request);
    fb.title = title;
    fb.name_buf[0] = '\0';
    navigate_to(fb, std::filesystem::is_directory(start_dir) ? start_dir : std::filesystem::current_path());
    if (const auto *save = std::get_if<SaveRequest>(&*fb.request))
      copy_to_buffer(fb.name_buf.data(), fb.name_buf.size(), save->default_name);
  }

  void navigate_to(FileBrowserState &fb, const std::filesystem::path &dir) {
    fb.current_dir = dir;
    if (find_open_request(fb) != nullptr)
      fb.name_buf[0] = '\0';
    refresh_entries(fb);
  }

  std::optional<std::filesystem::path> confirm_path(const FileBrowserState &fb) {
    if (!fb.request)
      return std::nullopt;

    if (std::holds_alternative<OpenRequest>(*fb.request)) {
      if (!fb.selected_entry || *fb.selected_entry >= fb.entries.size())
        return std::nullopt;
      const corundum::core::DirEntry &entry = fb.entries[*fb.selected_entry];
      if (entry.is_dir)
        return std::nullopt;
      return entry.path;
    }

    const std::string_view name{fb.name_buf.data()};
    if (name.empty())
      return std::nullopt;
    const std::string qualified = with_extension(name, std::get<SaveRequest>(*fb.request).extension);
    return fb.current_dir / qualified;
  }

} // namespace corundum::toolkit::widgets
