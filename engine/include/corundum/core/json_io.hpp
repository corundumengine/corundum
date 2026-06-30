#pragma once
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace corundum::core {

  /// Write a JSON value to @p path with sorted keys and 2-space indentation.
  /// Produces stable diffs — reordering fields in source code won't change output.
  /// @returns ok on success, or an error message if the file could not be opened or written.
  [[nodiscard]] std::expected<void, std::string> write_json(const std::filesystem::path &path, const nlohmann::json &j);

} // namespace corundum::core
