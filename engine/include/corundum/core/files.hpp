#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::core {

  /**
   * @brief Return the immediate children of @p dir.
   *
   * Empty on any error or invalid path.
   *
   * @param dir Directory to list.
   * @return Paths of immediate children, or empty on error.
   */
  [[nodiscard]] std::vector<std::filesystem::path> list_files_in_dir(const std::filesystem::path &dir);

  /// One directory entry as returned by list_dir_entries().
  struct DirEntry {
    std::filesystem::path path; ///< Full path of the entry.
    std::string name;           ///< Filename component only.
    bool is_dir;                ///< True if this entry is a directory.
  };

  /**
   * @brief Return the immediate children of @p dir as DirEntry (path + name + is_dir).
   *
   * Directories first, then alphabetically by name. Empty on any error or invalid path (matches
   * list_files_in_dir()'s silent-empty convention rather than propagating an error).
   *
   * @param dir Directory to list.
   * @return Sorted directory entries, or empty on error.
   */
  [[nodiscard]] std::vector<DirEntry> list_dir_entries(const std::filesystem::path &dir);

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
