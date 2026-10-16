// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/content_source.hpp>
#include <corundum/core/files.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iosfwd>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace corundum::core {

  std::filesystem::path DirectoryContentSource::resolve(const std::filesystem::path &path) const {
    return root_.empty() || path.is_absolute() ? path : root_ / path;
  }

  DirectoryContentSource::DirectoryContentSource(std::filesystem::path root) : root_(std::move(root)) {}

  std::expected<std::vector<std::uint8_t>, std::string>
  DirectoryContentSource::read(const std::filesystem::path &path) const {
    const std::filesystem::path resolved = resolve(path);
    std::ifstream f(resolved, std::ios::binary);
    if (!f)
      return std::unexpected(std::format("cannot open: {}", resolved.string()));

    f.seekg(0, std::ios::end);
    const std::streampos end = f.tellg();
    if (end < 0)
      return std::unexpected(std::format("read failed: {}", resolved.string()));

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    f.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
      // The stream reads chars; the buffer is the same bytes.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      f.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!f)
      return std::unexpected(std::format("read failed: {}", resolved.string()));

    return bytes;
  }

  bool DirectoryContentSource::contains(const std::filesystem::path &path) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(resolve(path), ec) && !ec;
  }

  std::expected<std::vector<DirEntry>, std::string> DirectoryContentSource::list(const std::filesystem::path &dir,
                                                                                 ListOptions options) const {
    std::expected<std::vector<DirEntry>, std::string> entries = list_dir_entries(resolve(dir), options);
    if (!entries)
      return entries;

    if (!root_.empty() && !dir.is_absolute()) {
      for (DirEntry &entry : *entries)
        entry.path = entry.path.lexically_relative(root_);
    }

    return entries;
  }

} // namespace corundum::core
