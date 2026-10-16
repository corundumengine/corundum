// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/content_source.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::test {

  /// In-memory ContentSource for loader tests. Files are keyed by generic-string path.
  ///
  /// A directory only exists here when at least one file lives under it, so an empty directory
  /// reads as missing (unlike DirectoryContentSource, which enumerates the real tree).
  class MemoryContentSource final : public corundum::core::ContentSource {
  public:
    /// Add a text fixture.
    /// @note Not thread-safe. Call before handing the source to anything that reads it.
    void add(std::string_view path, std::string_view contents) {
      const std::string key = std::filesystem::path(path).generic_string();
      files_.insert_or_assign(key, std::vector<std::uint8_t>(contents.begin(), contents.end()));
    }

    /// Add a binary fixture (WAV, fonts).
    /// @note Not thread-safe. Call before handing the source to anything that reads it.
    void add(std::string_view path, std::span<const std::uint8_t> bytes) {
      const std::string key = std::filesystem::path(path).generic_string();
      files_.insert_or_assign(key, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }

    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
    read(const std::filesystem::path &path) const override {
      const std::string key = path.generic_string();
      const auto it = files_.find(key);
      if (it == files_.end())
        return std::unexpected("cannot open: " + key);
      return it->second;
    }

    [[nodiscard]] bool contains(const std::filesystem::path &path) const override {
      return files_.contains(path.generic_string());
    }

    [[nodiscard]] std::expected<std::vector<corundum::core::DirEntry>, std::string>
    list(const std::filesystem::path &dir, corundum::core::ListOptions options) const override {
      std::string base = dir.generic_string();
      while (!base.empty() && base.back() == '/')
        base.pop_back();
      const std::string prefix = base + "/";

      // Existence is decided before filtering: a directory with files the filter later drops
      // still exists and must return an empty vector, not an error.
      const bool any_under_prefix =
          std::ranges::any_of(files_, [&](const auto &entry) { return entry.first.starts_with(prefix); });
      if (!any_under_prefix)
        return std::unexpected("Directory does not exist: " + dir.string());

      std::map<std::string, corundum::core::DirEntry> by_path;
      for (const auto &[key, _] : files_) {
        if (key.starts_with(prefix))
          insert_entries(by_path, dir, key.substr(prefix.size()), options.recursive);
      }

      return collect(by_path, options.extensions);
    }

  private:
    /// Insert the file at @p remainder and, depending on @p recursive, its directory entries.
    static void insert_entries(std::map<std::string, corundum::core::DirEntry> &by_path,
                               const std::filesystem::path &dir, std::string_view remainder, bool recursive) {
      if (!recursive) {
        const std::size_t slash = remainder.find('/');
        if (slash == std::string_view::npos)
          add_file(by_path, dir, remainder);
        else
          add_dir(by_path, dir, remainder.substr(0, slash));
        return;
      }

      // Recursive: every intermediate directory at any depth, then the file itself.
      for (std::size_t slash = remainder.find('/'); slash != std::string_view::npos;
           slash = remainder.find('/', slash + 1)) {
        add_dir(by_path, dir, remainder.substr(0, slash));
      }
      add_file(by_path, dir, remainder);
    }

    /// Apply the extension filter to files, then sort directories first and by name.
    static std::vector<corundum::core::DirEntry>
    collect(const std::map<std::string, corundum::core::DirEntry> &by_path,
            const std::vector<std::string_view> &extensions) {
      std::vector<corundum::core::DirEntry> entries;
      for (const auto &[_, entry] : by_path) {
        if (!entry.is_dir && !extensions.empty()) {
          const bool any_match = std::ranges::any_of(
              extensions, [&](std::string_view ext) { return corundum::core::has_extension(entry.path, ext); });
          if (!any_match)
            continue;
        }
        entries.push_back(entry);
      }

      std::ranges::sort(entries, [](const corundum::core::DirEntry &a, const corundum::core::DirEntry &b) {
        if (a.is_dir != b.is_dir)
          return a.is_dir;
        return a.name < b.name;
      });
      return entries;
    }

    static void add_file(std::map<std::string, corundum::core::DirEntry> &by_path, const std::filesystem::path &dir,
                         std::string_view remainder) {
      const std::filesystem::path file_path = dir / std::filesystem::path(remainder);
      const std::string name = file_path.filename().string();
      by_path.emplace(file_path.generic_string(),
                      corundum::core::DirEntry{.is_dir = false, .name = name, .path = file_path});
    }

    static void add_dir(std::map<std::string, corundum::core::DirEntry> &by_path, const std::filesystem::path &dir,
                        std::string_view component) {
      const std::filesystem::path dir_path = dir / std::filesystem::path(component);
      by_path.emplace(dir_path.generic_string(),
                      corundum::core::DirEntry{.is_dir = true, .name = std::string{component}, .path = dir_path});
    }

    std::flat_map<std::string, std::vector<std::uint8_t>, std::less<>> files_;
  };

  /// The real filesystem with paths as given — what every pre-existing test passes to a loader.
  [[nodiscard]] inline const core::ContentSource &disk() {
    static const corundum::core::DirectoryContentSource source;
    return source;
  }

} // namespace corundum::test
