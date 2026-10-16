// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace corundum::core {

  /**
   * @brief Per-user writable storage for settings and saves.
   *
   * Runtime code writes player data only through this port, so a console save-data API can
   * replace the directory adapter.
   *
   * Names are relative and `/`-separated: `settings.json`, `saves/slot_01.json`.
   */
  class UserStorage {
  public:
    UserStorage(const UserStorage &) = delete;
    UserStorage &operator=(const UserStorage &) = delete;
    UserStorage(UserStorage &&) = delete;
    UserStorage &operator=(UserStorage &&) = delete;
    virtual ~UserStorage() = default;

    [[nodiscard]] virtual std::expected<std::string, std::string> read(std::string_view name) const = 0;

    [[nodiscard]] virtual bool contains(std::string_view name) const = 0;

    /// Replace the whole entry atomically and create intermediate folders.
    /// @pre @p name is non-empty, relative, `/`-separated, and has no `..` component.
    [[nodiscard]] virtual std::expected<void, std::string> write(std::string_view name, std::string_view contents) = 0;

  protected:
    UserStorage() = default;
  };

  /// User storage rooted at a real directory tree.
  class DirectoryUserStorage final : public UserStorage {
  public:
    explicit DirectoryUserStorage(std::filesystem::path root);

    [[nodiscard]] std::expected<std::string, std::string> read(std::string_view name) const override;

    [[nodiscard]] bool contains(std::string_view name) const override;

    [[nodiscard]] std::expected<void, std::string> write(std::string_view name, std::string_view contents) override;

  private:
    std::filesystem::path root_;
  };

  /// A DirectoryUserStorage at user_data_dir(@p game_id).
  /// Error when @p game_id is empty or no user-data directory resolves (the message comes from
  /// user_data_dir). Does not create the directory.
  [[nodiscard]] std::expected<std::unique_ptr<UserStorage>, std::string> make_user_storage(std::string_view game_id);

  /// Read @p name through @p storage and parse it. Same messages as the content overload, naming @p name.
  [[nodiscard]] std::expected<nlohmann::json, std::string> read_json(const UserStorage &storage, std::string_view name,
                                                                     std::string_view label = {});

  /// Write @p j to @p name as format_json() does.
  [[nodiscard]] std::expected<void, std::string> write_json(UserStorage &storage, std::string_view name,
                                                            const nlohmann::json &j);

} // namespace corundum::core
