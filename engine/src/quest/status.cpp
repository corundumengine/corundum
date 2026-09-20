// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/flags.hpp>
#include <string_view>
#include <vector>

namespace corundum::quest {

  namespace {

    /// The stage whose sequence equals @p stage_seq, or nullptr. Non-positive
    /// sequences never name a stage (0 is the not-started sentinel).
    [[nodiscard]] const Stage *stage_for_sequence(const Quest &quest, int stage_seq) {
      if (stage_seq <= 0)
        return nullptr;
      for (const auto &stage : quest.stages) {
        if (stage.sequence == stage_seq)
          return &stage;
      }
      return nullptr;
    }

  } // namespace

  Lifecycle lifecycle(const Quest &quest, const corundum::world::FlagStore &flags) {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return Lifecycle::NotStarted;
    const Stage *stage = stage_for_sequence(quest, stage_seq);
    if (stage == nullptr)
      return Lifecycle::Active; // dangling sequence
    if (stage->failed)
      return Lifecycle::Failed;
    if (stage->resolved)
      return Lifecycle::Completed;
    return Lifecycle::Active;
  }

  const Stage *current_stage(const Quest &quest, const corundum::world::FlagStore &flags) {
    return stage_for_sequence(quest, get_stage(quest.quest_id, flags));
  }

  std::vector<ObjectiveView> objectives(const Quest &quest, const corundum::world::FlagStore &flags,
                                        const Registry *quests, std::string_view zone_id) {
    const auto *stage = current_stage(quest, flags);
    if (stage == nullptr)
      return {};

    std::vector<ObjectiveView> result;
    result.reserve(stage->objectives.size());
    for (const auto &objective : stage->objectives) {
      bool done = false;
      if (objective.done_condition.has_value())
        done = dialogue::evaluate(*objective.done_condition, flags, quests, {}, zone_id);
      result.push_back(
          ObjectiveView{.done = done, .has_condition = objective.done_condition.has_value(), .text = objective.text});
    }
    return result;
  }

} // namespace corundum::quest