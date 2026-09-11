#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace corundum::test {

  /// RAII owner of a scratch directory under the system temp path. The directory is
  /// created empty on construction and removed recursively on destruction, so a test
  /// never leaves a temp tree behind — including when a REQUIRE aborts the case early.
  class TempDir {
  public:
    explicit TempDir(std::string_view prefix, std::string_view tag)
        : path_(std::filesystem::temp_directory_path() / (std::string{prefix} + std::string{tag})) {
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

    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
    operator const std::filesystem::path &() const noexcept { return path_; }

    [[nodiscard]] std::filesystem::path operator/(std::string_view child) const {
      return path_ / std::filesystem::path{child};
    }

  private:
    std::filesystem::path path_;
  };

} // namespace corundum::test
