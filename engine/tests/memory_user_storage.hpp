// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/user_storage.hpp>

#include <expected>
#include <flat_map>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::test {

  /// In-memory UserStorage for tests.
  ///
  /// @c entries is public so tests can seed and inspect it. @c injected_write_error lets a test
  /// force every write to fail without touching the filesystem.
  class MemoryUserStorage final : public corundum::core::UserStorage {
  public:
    std::flat_map<std::string, std::string, std::less<>> entries;

    std::optional<std::string> injected_write_error;

    [[nodiscard]] std::expected<std::string, std::string> read(std::string_view name) const override {
      const auto it = entries.find(name);
      if (it == entries.end())
        return std::unexpected("cannot open: " + std::string{name});
      return it->second;
    }

    [[nodiscard]] bool contains(std::string_view name) const override {
      return entries.contains(name);
    }

    [[nodiscard]] std::expected<void, std::string> write(std::string_view name, std::string_view contents) override {
      if (injected_write_error)
        return std::unexpected(*injected_write_error);
      entries.insert_or_assign(std::string{name}, std::string{contents});
      return {};
    }
  };

} // namespace corundum::test
