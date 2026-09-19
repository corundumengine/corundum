// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/quest/quest.hpp>

#include <cstddef>
#include <filesystem>
#include <flat_map>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::quest {

  /** @brief Owns all loaded Quest objects for the session.
   *
   * Keys are the "id" field from each JSON file (== Quest::quest_id).
   */
  class Registry {
  public:
    /**
     * @brief Scan a directory for *.json files and load each as a Quest.
     *
     * Bad files are skipped with a stderr message (non-fatal). Existing
     * entries are not cleared first, so repeated calls accumulate.
     *
     * @param dir Directory containing quest JSON files.
     * @return Number of quests successfully loaded.
     * @note A missing or unreadable directory is also reported as 0, with the
     *       underlying error written to stderr.
     */
    [[nodiscard]] int load_all(const std::filesystem::path &dir);

    /**
     * @brief Look up a quest by id.
     * @param quest_id The quest's machine-readable identifier.
     * @return Pointer to the Quest, or nullptr if not found. O(log n).
     */
    [[nodiscard]] const Quest *find(std::string_view quest_id) const;

    /** @brief Number of loaded quests. */
    [[nodiscard]] std::size_t size() const noexcept {
      return quests_.size();
    }

    /** @brief Register a quest directly, keyed by its quest_id.
     *
     *  Unlike load_all(), this path does not run quest::validate.
     *
     *  @param quest The quest to register (moved into the registry).
     *  @return True if the quest was inserted; false if a quest with the same
     *          quest_id was already present (first registration wins).
     *  @pre `quest` has already passed quest::validate. Useful for tests and
     *       callers holding an already-validated quest.
     */
    bool add(Quest quest) {
      const std::string id = quest.quest_id;
      return quests_.emplace(id, std::move(quest)).second;
    }

    /** @brief Range-for support for iterating all loaded quests (id, quest) pairs. */
    auto begin() const noexcept {
      return quests_.begin();
    }

    /** @brief Iterator past the last loaded quest. */
    auto end() const noexcept {
      return quests_.end();
    }

  private:
    std::flat_map<std::string, Quest, std::less<>> quests_;
  };

} // namespace corundum::quest
