// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/quest/quest.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/world/flags.hpp>

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::quest {

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
  [[nodiscard]] int get_stage(std::string_view quest_id, const corundum::world::FlagStore &flags);

  /**
   * @brief Check whether the current stage is a resolved ending.
   *
   * A failed stage is also resolved, so this reports true for both success and
   * failure endings; is_failed() tells them apart.
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return True when the stage named by the quest's flag is resolved.
   */
  [[nodiscard]] bool is_resolved(const Quest &quest, const corundum::world::FlagStore &flags);

  /**
   * @brief Check whether the current stage is a failure ending.
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return True when the stage named by the quest's flag is failed.
   */
  [[nodiscard]] bool is_failed(const Quest &quest, const corundum::world::FlagStore &flags);

  /**
   * @brief Start a quest by setting its flag to the first stage's sequence.
   *
   * No-op if the quest is already started (flag > 0). A quest with no stages, or
   * whose first stage has a non-positive sequence, cannot be started and is
   * reported to stderr instead. This only affects quests registered through
   * Registry::add(), which skips quest::validate().
   *
   * @param quest The quest to start.
   * @param flags Active FlagStore to mutate.
   */
  void start(const Quest &quest, corundum::world::FlagStore &flags);

  /**
   * @brief Advance a quest to a named stage.
   *
   * Works on unstarted quests (allows organic discovery to bypass the
   * start stage). An unknown stage_name is a no-op reported to stderr in every
   * build; in debug builds, a transition to a stage not listed in the current
   * stage's advances_to additionally warns, to catch dialogue typos.
   *
   * @param quest      The quest definition.
   * @param stage_name The name of the target stage.
   * @param flags      Active FlagStore to mutate.
   */
  void advance(const Quest &quest, std::string_view stage_name, corundum::world::FlagStore &flags);

  /**
   * @brief Collect pointers to every quest that has been started (stage > 0).
   *
   * Includes in-progress, completed, and failed quests — not only
   * Lifecycle::Active ones. Use lifecycle() to filter further.
   *
   * @param registry The quest registry to scan.
   * @param flags    Active FlagStore.
   * @return Non-owning pointers to all started quests; borrows @p registry.
   */
  [[nodiscard]] std::vector<const Quest *> started_quests(const Registry &registry,
                                                          const corundum::world::FlagStore &flags);

  /**
   * @brief Advance quests whose current stage's objectives are all satisfied.
   *
   * For each quest on an active (non-resolved) stage that carries an
   * `auto_advance_to`, if that stage has at least one conditioned objective and
   * every `done_condition` evaluates true (order-independent), advances to the
   * named target stage. Stages without `auto_advance_to` are inert.
   * Idempotent: once a quest resolves or its target stage's
   * conditions stop holding, later calls are no-ops.
   *
   * @param registry The quest registry to scan.
   * @param flags    Active FlagStore to mutate.
   * @param zone_id  Current zone; `local.<key>` done_conditions resolve against it.
   */
  void tick_quests(const Registry &registry, corundum::world::FlagStore &flags, std::string_view zone_id = {});

} // namespace corundum::quest
