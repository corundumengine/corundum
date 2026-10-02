// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

namespace corundum::test {

  namespace detail {
    /// Per-process token mixed into every TempDir path. Two test processes must never share a
    /// scratch directory even when they use the same prefix/tag — the save tests keep one shared
    /// root across their cases, and CTest runs each case as its own process.
    [[nodiscard]] inline const std::string &process_token() {
      static const std::string token = [] {
        std::random_device random;
        return std::to_string((static_cast<std::uint64_t>(random()) << 32U) | static_cast<std::uint64_t>(random()));
      }();
      return token;
    }
  } // namespace detail

  /// RAII owner of a scratch directory under the system temp path. The directory is
  /// created empty on construction and removed recursively on destruction, so a test
  /// never leaves a temp tree behind — including when a REQUIRE aborts the case early.
  ///
  /// The path includes a per-process token, so parallel CTest cases cannot remove each
  /// other's scratch trees.
  class TempDir {
  public:
    explicit TempDir(std::string_view prefix, std::string_view tag)
        : path_(std::filesystem::temp_directory_path() /
                (std::string{prefix} + std::string{tag} + "_" + detail::process_token())) {
      std::filesystem::remove_all(path_);
      std::filesystem::create_directories(path_);
    }

    ~TempDir() {
      std::error_code ec;
      std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    TempDir(TempDir &&) = delete;
    TempDir &operator=(TempDir &&) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept {
      return path_;
    }

    // Implicit conversion is intentional: callers pass the scratch dir straight to APIs
    // expecting a path (e.g. Registry::load_all(dir)) without an explicit .path().
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor, misc-explicit-constructor)
    operator const std::filesystem::path &() const noexcept {
      return path_;
    }

    [[nodiscard]] std::filesystem::path operator/(std::string_view child) const {
      return path_ / std::filesystem::path{child};
    }

  private:
    std::filesystem::path path_;
  };

} // namespace corundum::test
