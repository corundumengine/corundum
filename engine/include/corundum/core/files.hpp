#pragma once

#include <filesystem>
#include <vector>

namespace corundum::core {

  /// Returns the immediate children of @p dir; empty on any error or invalid path.
  [[nodiscard]] std::vector<std::filesystem::path> list_files_in_dir(const std::filesystem::path &dir);

} // namespace corundum::core
