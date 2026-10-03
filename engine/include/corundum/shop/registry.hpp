// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/shop/shop.hpp>

#include <cstddef>
#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::shop {

  /** @brief Owns all loaded Shop objects for the session, keyed by id. */
  class Registry {
  public:
    /** @brief Load every `*.json` batch file directly under @p dir.
     *
     *  Bad files and duplicate ids are skipped with a stderr message (non-fatal).
     *
     *  @param dir Directory containing shop batch files.
     *  @return Number of shops successfully loaded.
     */
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /** @brief Look up a shop by id.
     *  @return Pointer to the shop, or nullptr if not found. O(log n). */
    [[nodiscard]] const Shop *find(std::string_view id) const;

    /** @brief Number of loaded shops. */
    [[nodiscard]] std::size_t size() const noexcept {
      return shops_.size();
    }

    /** @brief Register a shop directly, keyed by id. First registration wins.
     *  @return True if inserted; false when the id was already present. */
    bool add(Shop shop) {
      const std::string id = shop.id;
      return shops_.emplace(id, std::move(shop)).second;
    }

    /** @brief Range-for support for iterating all (id, shop) pairs. */
    [[nodiscard]] auto begin() const noexcept {
      return shops_.begin();
    }

    /** @brief Iterator past the last loaded shop. */
    [[nodiscard]] auto end() const noexcept {
      return shops_.end();
    }

  private:
    std::flat_map<std::string, Shop, std::less<>> shops_;
  };

} // namespace corundum::shop
