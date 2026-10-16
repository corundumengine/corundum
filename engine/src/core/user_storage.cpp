// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/user_storage.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/core/user_data_dir.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <memory>
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): read_json returns an expected<nlohmann::json>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace corundum::core {

  DirectoryUserStorage::DirectoryUserStorage(std::filesystem::path root) : root_(std::move(root)) {}

  std::expected<std::string, std::string> DirectoryUserStorage::read(std::string_view name) const {
    const std::filesystem::path path = root_ / std::filesystem::path(name);
    const std::ifstream f(path, std::ios::binary);
    if (!f)
      return std::unexpected(std::format("cannot open: {}", path.string()));

    std::ostringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
  }

  bool DirectoryUserStorage::contains(std::string_view name) const {
    std::error_code ec;
    return std::filesystem::is_regular_file(root_ / std::filesystem::path(name), ec) && !ec;
  }

  std::expected<void, std::string> DirectoryUserStorage::write(std::string_view name, std::string_view contents) {
    const std::filesystem::path path = root_ / std::filesystem::path(name);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
      return std::unexpected(std::format("cannot create '{}': {}", path.parent_path().string(), ec.message()));

    return write_file_atomic(path, contents);
  }

  std::expected<std::unique_ptr<UserStorage>, std::string> make_user_storage(std::string_view game_id) {
    std::expected<std::filesystem::path, std::string> root = user_data_dir(game_id);
    if (!root)
      return std::unexpected(root.error());
    return std::make_unique<DirectoryUserStorage>(std::move(*root));
  }

  std::expected<nlohmann::json, std::string> read_json(const UserStorage &storage, std::string_view name,
                                                       std::string_view label) {
    const std::expected<std::string, std::string> text = storage.read(name);
    if (!text) {
      if (label.empty())
        return std::unexpected(text.error());
      return std::unexpected(std::format("{}: {}", label, text.error()));
    }
    return parse_json(*text, name, label);
  }

  std::expected<void, std::string> write_json(UserStorage &storage, std::string_view name, const nlohmann::json &j) {
    return storage.write(name, format_json(j));
  }

} // namespace corundum::core
