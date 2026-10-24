// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/user_storage.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/core/user_data_dir.hpp>

#include <algorithm>
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

  namespace {

    /// A storage name must stay inside root_: non-empty, with no root component (a leading '/'
    /// or "C:" would replace root_'s root in operator/) and no ".." component.
    [[nodiscard]] bool is_valid_storage_name(std::string_view name) {
      const std::filesystem::path relative{name};
      return !name.empty() && !relative.has_root_path() &&
             std::ranges::none_of(relative, [](const std::filesystem::path &part) { return part == ".."; });
    }

    [[nodiscard]] std::string invalid_storage_name_message(std::string_view name) {
      return std::format("invalid storage name '{}' (must be non-empty, relative, with no '..' component)", name);
    }

    /// Join a `/`-separated storage name onto @p root using the platform's separator, so
    /// error messages show one consistent path (`saves/slot_01.json` would otherwise keep
    /// its forward slashes next to the root's backslashes).
    [[nodiscard]] std::filesystem::path native_path(const std::filesystem::path &root, std::string_view name) {
      std::filesystem::path path = root;
      for (const std::filesystem::path &part : std::filesystem::path(name))
        path /= part;
      return path;
    }

  } // namespace

  DirectoryUserStorage::DirectoryUserStorage(std::filesystem::path root) : root_(std::move(root)) {}

  std::expected<std::string, std::string> DirectoryUserStorage::read(std::string_view name) const {
    if (!is_valid_storage_name(name))
      return std::unexpected(invalid_storage_name_message(name));

    const std::filesystem::path path = native_path(root_, name);
    const std::ifstream f(path, std::ios::binary);
    if (!f)
      return std::unexpected(std::format("cannot open: {}", path.string()));

    std::ostringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
  }

  bool DirectoryUserStorage::contains(std::string_view name) const {
    if (!is_valid_storage_name(name))
      return false;

    std::error_code ec;
    return std::filesystem::is_regular_file(native_path(root_, name), ec) && !ec;
  }

  std::expected<void, std::string> DirectoryUserStorage::write(std::string_view name, std::string_view contents) {
    if (!is_valid_storage_name(name))
      return std::unexpected(invalid_storage_name_message(name));

    const std::filesystem::path path = native_path(root_, name);

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

  // clang-tidy maps nlohmann::json to json_fwd.hpp; json.hpp above is the full definition it needs.
  // NOLINTNEXTLINE(misc-include-cleaner)
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
