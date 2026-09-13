// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cctype>
#include <corundum/core/files.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace corundum::core {

  std::expected<std::vector<DirEntry>, std::string> list_dir_entries(const std::filesystem::path &dir,
                                                                     ListOptions options) {
    std::error_code ec;
    const std::filesystem::file_status dir_status = std::filesystem::status(dir, ec);
    if (ec)
      return std::unexpected(std::format("Error accessing {}: {}", dir.string(), ec.message()));
    if (dir_status.type() == std::filesystem::file_type::not_found)
      return std::unexpected(std::format("Directory does not exist: {}", dir.string()));
    if (!std::filesystem::is_directory(dir_status))
      return std::unexpected(std::format("Path is not a directory: {}", dir.string()));

    std::vector<DirEntry> result;

    const auto visit = [&](const std::filesystem::directory_entry &entry) {
      std::error_code entry_ec;
      const bool entry_is_dir = entry.is_directory(entry_ec);
      if (entry_ec)
        return;
      if (!entry_is_dir && !options.extensions.empty()) {
        const bool any_match = std::ranges::any_of(
            options.extensions, [&](std::string_view ext) { return has_extension(entry.path(), ext); });
        if (!any_match)
          return;
      }
      result.push_back({.is_dir = entry_is_dir, .name = entry.path().filename().string(), .path = entry.path()});
    };

    if (options.recursive) {
      for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        visit(*it);
    } else {
      for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        visit(*it);
    }

    if (ec)
      return std::unexpected(std::format("Error walking {}: {}", dir.string(), ec.message()));

    std::ranges::sort(result, [](const DirEntry &a, const DirEntry &b) {
      if (a.is_dir != b.is_dir)
        return a.is_dir;
      return a.name < b.name;
    });

    return result;
  }

  namespace {

    std::string_view strip_leading_dot(std::string_view extension) {
      if (!extension.empty() && extension.front() == '.')
        return extension.substr(1);
      return extension;
    }

  } // namespace

  bool has_extension(const std::filesystem::path &path, std::string_view ext) noexcept {
    const std::string actual = path.extension().string();
    const std::string_view actual_ext = strip_leading_dot(actual);
    const std::string_view expected_ext = strip_leading_dot(ext);
    if (actual_ext.size() != expected_ext.size())
      return false;
    return std::ranges::equal(actual_ext, expected_ext,
                              [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
  }

} // namespace corundum::core
