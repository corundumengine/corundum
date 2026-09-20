// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/quest/quest.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace corundum::quest {

  namespace {

    /// Append the "unknown stage" error for @p target, naming the field it came
    /// from ("advances_to" or "auto_advance_to") in the message.
    void check_target(const Quest &quest, std::string_view stage_name, std::string_view field, std::string_view target,
                      std::vector<std::string> &errors) {
      if (quest.find_stage(target) == nullptr)
        errors.push_back(std::format(R"("{}": stage "{}" "{}" targets unknown stage "{}")", quest.quest_id, stage_name,
                                     field, target));
    }

    /// Append an error for each cycle in the auto-advance graph. A cycle re-enters its
    /// stages every tick, re-recording their breadcrumbs without bound — the same
    /// failure a self-target causes, and one the per-stage check alone misses.
    void check_auto_advance_cycles(const Quest &quest, std::vector<std::string> &errors) {
      // 0 = unseen, 1 = on the current walk, 2 = settled (walked, provably acyclic).
      std::unordered_map<std::string_view, int> state;

      for (const auto &origin : quest.stages) {
        if (!origin.auto_advance_to.has_value())
          continue;

        // A self-target is reported by the per-stage check; settle it so a chain
        // feeding into it does not re-report the same loop.
        if (*origin.auto_advance_to == origin.name) {
          state[origin.name] = 2;
          continue;
        }

        std::vector<const Stage *> path;
        const Stage *node = &origin;
        while (node != nullptr) {
          int &node_state = state.try_emplace(node->name, 0).first->second;
          if (node_state == 2)
            break;
          if (node_state == 1) {
            errors.push_back(
                std::format(R"("{}": auto_advance_to cycle through stage "{}")", quest.quest_id, node->name));
            break;
          }
          node_state = 1;
          path.push_back(node);
          node = node->auto_advance_to.has_value() ? quest.find_stage(*node->auto_advance_to) : nullptr;
        }

        for (const Stage *reached : path)
          state[reached->name] = 2;
      }
    }

  } // namespace

  ValidationResult validate(const Quest &quest) {
    ValidationResult result;

    if (quest.quest_id.empty())
      result.errors.emplace_back("quest has an empty quest_id");

    // The schema requires a non-empty name, so an empty one would serialize to a
    // file load_quest() rejects.
    if (quest.name.empty())
      result.errors.emplace_back("quest has an empty name");

    std::vector<std::string> stage_names =
        quest.stages | std::views::transform(&Stage::name) | std::ranges::to<std::vector>();
    std::ranges::sort(stage_names);
    const auto duplicate_name = std::ranges::adjacent_find(stage_names);
    if (duplicate_name != stage_names.end())
      result.errors.push_back(std::format(R"("{}": duplicate stage name "{}")", quest.quest_id, *duplicate_name));

    std::vector<int> stage_sequences =
        quest.stages | std::views::transform(&Stage::sequence) | std::ranges::to<std::vector>();
    std::ranges::sort(stage_sequences);
    const auto duplicate_sequence = std::ranges::adjacent_find(stage_sequences);
    if (duplicate_sequence != stage_sequences.end())
      result.errors.push_back(std::format(R"("{}": duplicate stage sequence {})", quest.quest_id, *duplicate_sequence));

    if (!std::ranges::any_of(quest.stages, &Stage::resolved))
      result.errors.push_back(std::format("\"{}\" has no resolved stage", quest.quest_id));

    for (const auto &stage : quest.stages) {
      if (stage.name.empty())
        result.errors.push_back(std::format(R"("{}": stage has an empty name)", quest.quest_id));

      // Zero is the "not started" sentinel get_stage() reports, so a non-positive
      // sequence is a stage the runtime can never enter.
      if (stage.sequence <= 0)
        result.errors.push_back(std::format(R"("{}": stage "{}" has sequence {}, which must be positive)",
                                            quest.quest_id, stage.name, stage.sequence));

      // A failed ending must also be resolved: is_resolved() reads only that flag, so
      // without it a failure would report as an unfinished quest. Loaders normalize the
      // pair; this catches a quest built or edited in memory.
      if (stage.failed && !stage.resolved)
        result.errors.push_back(
            std::format(R"("{}": stage "{}" is failed but not resolved)", quest.quest_id, stage.name));

      for (const auto &target : stage.advances_to)
        check_target(quest, stage.name, "advances_to", target, result.errors);
      const std::optional<std::string> &auto_target = stage.auto_advance_to;
      if (auto_target.has_value()) {
        check_target(quest, stage.name, "auto_advance_to", *auto_target, result.errors);
        // A self-target re-arms every tick: tick_quests would re-enter the same stage
        // and re-record its seen breadcrumb without bound.
        if (*auto_target == stage.name)
          result.errors.push_back(
              std::format(R"("{}": stage "{}" auto_advance_to targets itself)", quest.quest_id, stage.name));
      }
    }

    check_auto_advance_cycles(quest, result.errors);

    for (std::size_t i = 1; i < quest.stages.size(); ++i) {
      const Stage &previous = quest.stages[i - 1];
      const Stage &current = quest.stages[i];
      // Equality is a duplicate sequence, already reported as an error above.
      if (current.sequence < previous.sequence)
        result.warnings.push_back(std::format("\"{}\": stage order \"{}\" (seq {}) is not after \"{}\" (seq {}) — "
                                              "sequence order does not match stage order",
                                              quest.quest_id, current.name, current.sequence, previous.name,
                                              previous.sequence));
    }

    return result;
  }

} // namespace corundum::quest
