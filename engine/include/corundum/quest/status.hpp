// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/quest/quest.hpp>
#include <corundum/world/flags.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace corundum::quest {

  class Registry;

  /**
   * @brief Typed lifecycle status of a quest, derived from the FlagStore.
   *
   * NotStarted — no stage integer written yet (flag absent or 0).
   * Active     — in progress on a non-resolved stage.
   * Completed  — on a resolved, non-failed stage.
   * Failed     — on a failed stage (which is also resolved).
   */
  enum class Lifecycle : uint8_t { NotStarted, Active, Completed, Failed };

  /**
   * @brief Derive the typed lifecycle of a quest from the current flag value.
   *
   * Pure derivation over the `quest.<id>` integer plus the quest's stage
   * definitions; it never mutates state.
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return The matching Lifecycle value.
   */
  [[nodiscard]] Lifecycle lifecycle(const Quest &quest, const corundum::world::FlagStore &flags) noexcept;

  /**
   * @brief The stage matching the current flag value, or nullptr when inactive.
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @return Pointer to the stage whose sequence equals the flag, or nullptr when
   *         the quest is not started or the sequence matches no stage.
   */
  [[nodiscard]] const Stage *current_stage(const Quest &quest, const corundum::world::FlagStore &flags) noexcept;

  /**
   * @brief Journal view of one objective of the current stage.
   */
  struct ObjectiveView {
    std::string_view text; ///< Journal text (view into the Quest definition).
    bool done;             ///< True when done_condition evaluates true.
    bool has_condition;    ///< True when the objective carries a done_condition.
  };

  /**
   * @brief Objectives of the current stage with their done-state.
   *
   * An objective is `done` when it has a done_condition that evaluates true
   * against @p flags (via the dialogue condition grammar). Bare objectives
   * report has_condition=false and are never auto-done here — they are
   * journal-display only.
   *
   * @param quest The quest definition.
   * @param flags Active FlagStore.
   * @param quests Registry used to resolve quest-helper conditions; may be
   *               nullptr (quest helpers then evaluate to false).
   * @param zone_id Current zone; `local.<key>` done_conditions resolve against it.
   * @return One ObjectiveView per objective of the current stage; empty when
   *         the quest is not started.
   * @note The returned views borrow @p quest's strings — the quest must outlive
   *       the returned vector.
   */
  [[nodiscard]] std::vector<ObjectiveView> objectives(const Quest &quest, const corundum::world::FlagStore &flags,
                                                      const Registry *quests = nullptr, std::string_view zone_id = {});

} // namespace corundum::quest