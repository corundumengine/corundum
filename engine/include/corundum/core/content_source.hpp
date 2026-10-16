// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/files.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::core {

  /**
   * @brief Read-only port over game content.
   *
   * Runtime code reads game content only through this port, so a pack file or a console
   * filesystem can replace the directory adapter without touching any loader.
   *
   * Paths are relative to the source's root. An absolute path is accepted as-is (tests and
   * tools pass temp-dir paths).
   *
   * Implementations must keep no unsynchronised mutable state, so the `const` methods are safe
   * to call from any thread. Miniaudio's resource-manager job thread can open audio through
   * them. An adapter that needs a cache must guard it with a mutex.
   */
  class ContentSource {
  public:
    ContentSource(const ContentSource &) = delete;
    ContentSource &operator=(const ContentSource &) = delete;
    ContentSource(ContentSource &&) = delete;
    ContentSource &operator=(ContentSource &&) = delete;
    virtual ~ContentSource() = default;

    /// Read the whole file at @p path. The error names the path.
    /// @note Every failure is reported through the returned error and must not throw anything
    ///       except std::bad_alloc. Callers in a `noexcept` C callback catch only std::bad_alloc.
    [[nodiscard]] virtual std::expected<std::vector<std::uint8_t>, std::string>
    read(const std::filesystem::path &path) const = 0;

    /// True only for an existing regular file. A directory is false.
    [[nodiscard]] virtual bool contains(const std::filesystem::path &path) const = 0;

    /// List @p dir with the same ordering, filtering and error cases as list_dir_entries().
    /// Each returned DirEntry::path is a content path that read() accepts.
    ///
    /// A separate pure virtual, not synthesized from read()/contains(), because those two can't
    /// enumerate a directory. Each adapter enumerates natively (the directory adapter through
    /// list_dir_entries, a future pack adapter through its index).
    [[nodiscard]] virtual std::expected<std::vector<DirEntry>, std::string> list(const std::filesystem::path &dir,
                                                                                 ListOptions options) const = 0;

  protected:
    ContentSource() = default;
  };

  /// Content source over a real directory tree.
  ///
  /// An empty root, the default, resolves paths exactly as given (cwd-relative or absolute),
  /// which is today's desktop behaviour. A non-empty root prefixes relative paths, and list()
  /// returns root-relative paths.
  class DirectoryContentSource final : public ContentSource {
  public:
    /// Empty root: paths resolve exactly as given (cwd-relative or absolute).
    DirectoryContentSource() = default;

    explicit DirectoryContentSource(std::filesystem::path root);

    [[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string>
    read(const std::filesystem::path &path) const override;

    [[nodiscard]] bool contains(const std::filesystem::path &path) const override;

    [[nodiscard]] std::expected<std::vector<DirEntry>, std::string> list(const std::filesystem::path &dir,
                                                                         ListOptions options) const override;

  private:
    [[nodiscard]] std::filesystem::path resolve(const std::filesystem::path &path) const;

    std::filesystem::path root_;
  };

} // namespace corundum::core
