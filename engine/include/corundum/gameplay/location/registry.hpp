// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/location/location.hpp>

#include <cstddef>
#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::gameplay::location {

  /** @brief Owns all loaded Location objects for the session, keyed by id. */
  class Registry {
  public:
    /** @brief Load every `*.json` batch file directly under @p dir.
     *
     *  Bad files and duplicate ids are skipped with a stderr message (non-fatal).
     *
     *  @param dir Directory containing location batch files.
     *  @return Number of locations successfully loaded.
     */
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /** @brief Look up a location by id.
     *  @return Pointer to the location, or nullptr if not found. O(log n). */
    [[nodiscard]] const Location *find(std::string_view id) const;

    /** @brief Number of loaded locations. */
    [[nodiscard]] std::size_t size() const noexcept {
      return locations_.size();
    }

    /** @brief Register a location directly, keyed by id. First registration wins.
     *  @return True if inserted; false when the id was already present. */
    bool add(Location location) {
      const std::string id = location.id;
      return locations_.emplace(id, std::move(location)).second;
    }

    /** @brief Range-for support for iterating all (id, location) pairs. */
    [[nodiscard]] auto begin() const noexcept {
      return locations_.begin();
    }

    /** @brief Iterator past the last loaded location. */
    [[nodiscard]] auto end() const noexcept {
      return locations_.end();
    }

  private:
    std::flat_map<std::string, Location, std::less<>> locations_;
  };

} // namespace corundum::gameplay::location
