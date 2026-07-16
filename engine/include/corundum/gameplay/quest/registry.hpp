#pragma once

#include <corundum/gameplay/quest/quest.hpp>

#include <filesystem>
#include <flat_map>
#include <string>
#include <string_view>

namespace corundum::gameplay::quest {

  /** @brief Owns all loaded Quest objects for the session.
   *
   * Keys are the "id" field from each JSON file (== Quest::quest_id).
   * Mirrors the dialogue::Registry pattern exactly.
   */
  class Registry {
  public:
    /**
     * @brief Scan a directory for *.json files and load each as a Quest.
     *
     * Bad files are skipped with a stderr message (non-fatal).
     *
     * @param dir Directory containing quest JSON files.
     * @return Number of quests successfully loaded.
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

    /** @brief Register a quest directly.
     *
     *  @param quest The quest to register (moved into the registry, keyed
     *               by quest_id). Useful for tests.
     */
    void add(Quest quest) {
      quests_.emplace(quest.quest_id, std::move(quest));
    }

    /** @brief Range-for support for iterating all loaded quests (id, quest) pairs. */
    [[nodiscard]] auto begin() const noexcept {
      return quests_.begin();
    }

    /** @brief Range-for support end sentinel. */
    [[nodiscard]] auto end() const noexcept {
      return quests_.end();
    }

  private:
    std::flat_map<std::string, Quest, std::less<>> quests_;
  };

} // namespace corundum::gameplay::quest
