#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::core {

  /// One directory entry as returned by list_dir_entries().
  struct DirEntry {
    bool is_dir;                ///< True if this entry is a directory.
    std::string name;           ///< Filename component only.
    std::filesystem::path path; ///< Full path of the entry.
  };

  /// Options controlling how list_dir_entries() walks a directory.
  struct ListOptions {
    /// Extensions to include; empty matches all. Compared case-insensitively (no leading dot).
    std::vector<std::string_view> extensions;

    /// If true, walk subdirectories recursively. Subdirectories themselves are always reported;
    /// the @p extensions filter applies only to non-directory entries.
    bool recursive = false;
  };

  /**
   * @brief Return the immediate children (or recursive subtree) of @p dir as DirEntry.
   *
   * Returned entries are sorted directories-first, then alphabetically by @c name. On a missing
   * directory, a path that exists but isn't a directory, or an iteration error, the returned
   * expected holds an error string instead of silently producing an empty list — callers that
   * want the legacy silent-empty behavior can check @c .has_value() and ignore the error.
   *
   * @param dir Directory to list.
   * @param opts Listing options (recursive walk, extension filter).
   * @return Sorted directory entries, or an error message.
   */
  [[nodiscard]] std::expected<std::vector<DirEntry>, std::string> list_dir_entries(const std::filesystem::path &dir,
                                                                                   ListOptions opts = {});

  /**
   * @brief Check whether @p path's extension matches @p ext.
   *
   * Comparison is case-insensitive and ignores any leading dot on either side (pass "json", not
   * ".json").
   *
   * @param path Path to test.
   * @param ext Expected extension (without leading dot).
   * @return True if the path's extension matches.
   */
  [[nodiscard]] bool has_extension(const std::filesystem::path &path, std::string_view ext) noexcept;

} // namespace corundum::core
