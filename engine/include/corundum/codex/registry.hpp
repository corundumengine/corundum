// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/codex/codex.hpp>

#include <cstddef>
#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::codex {

  /** @brief Owns all loaded CodexEntry objects for the session.
   *
   * Keys are the entry's `id`. Mirrors the quest::Registry pattern.
   */
  class Registry {
  public:
    /** @brief Load every `*.json` batch file directly under @p dir.
     *
     *  Bad files and duplicate ids are skipped with a stderr message (non-fatal).
     *
     *  @param dir Directory containing codex batch files.
     *  @return Number of entries successfully loaded.
     */
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /** @brief Look up an entry by id.
     *  @return Pointer to the entry, or nullptr if not found. O(log n). */
    [[nodiscard]] const CodexEntry *find(std::string_view id) const;

    /** @brief Number of loaded entries. */
    [[nodiscard]] std::size_t size() const noexcept {
      return entries_.size();
    }

    /** @brief Register an entry directly, keyed by id. First registration wins.
     *  @return True if inserted; false when the id was already present. */
    bool add(CodexEntry entry) {
      const std::string id = entry.id;
      return entries_.emplace(id, std::move(entry)).second;
    }

    /** @brief Range-for support for iterating all (id, entry) pairs. */
    [[nodiscard]] auto begin() const noexcept {
      return entries_.begin();
    }

    /** @brief Iterator past the last loaded entry. */
    [[nodiscard]] auto end() const noexcept {
      return entries_.end();
    }

  private:
    std::flat_map<std::string, CodexEntry, std::less<>> entries_;
  };

} // namespace corundum::codex
