#pragma once

#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::quest {

  /**
   * @brief Build the FlagStore key for a quest's stage integer.
   *
   * The key follows the pattern "quest.{quest_id}" and is used to read
   * and write the current stage for a quest.
   *
   * @param quest_id The quest's machine-readable identifier.
   * @return The flag key string.
   */
  [[nodiscard]] inline std::string quest_flag_key(std::string_view quest_id) {
    return std::format("quest.{}", quest_id);
  }

  /**
   * @brief Read the current stage sequence for a quest.
   *
   * @param quest_id The quest's machine-readable identifier.
   * @param flags    Active FlagStore.
   * @return The stage sequence (0 if not started / inactive).
   */
  [[nodiscard]] int get_stage(std::string_view quest_id, const FlagStore &flags) noexcept;

  /**
   * @brief Check whether a quest is over (flag matches any resolved stage).
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return True when the quest's current flag value matches a resolved stage.
   */
  [[nodiscard]] bool is_complete(const Quest &quest, const FlagStore &flags) noexcept;

  /**
   * @brief Check whether a quest ended in failure (flag matches any failed stage).
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return True when the quest's current flag value matches a failed stage.
   */
  [[nodiscard]] bool is_failed(const Quest &quest, const FlagStore &flags) noexcept;

  /**
   * @brief Start a quest by setting its flag to the first stage's sequence.
   *
   * No-op if the quest is already started (flag > 0).
   *
   * @param quest The quest to start.
   * @param flags Active FlagStore to mutate.
   */
  void start(const Quest &quest, FlagStore &flags);

  /**
   * @brief Advance a quest to a named stage.
   *
   * Works on unstarted quests (allows organic discovery to bypass the
   * start stage). No-op if stage_name is not found; prints a warning
   * in debug builds to catch dialogue typos.
   *
   * @param quest      The quest definition.
   * @param stage_name The name of the target stage.
   * @param flags      Active FlagStore to mutate.
   */
  void advance(const Quest &quest, std::string_view stage_name, FlagStore &flags);

  /**
   * @brief Collect pointers to all quests that are active (stage > 0).
   *
   * Iterates the registry and returns quests whose flag is set to a
   * positive value (started, in-progress, or resolved).
   *
   * @param registry The quest registry to scan.
   * @param flags    Active FlagStore.
   * @return Non-owning pointers to all active quests.
   */
  [[nodiscard]] std::vector<const Quest *> active_quests(const Registry &registry, const FlagStore &flags);

} // namespace corundum::gameplay::quest
