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
#include <vector>

namespace corundum::quest {

  namespace {

    /// Append the "unknown stage" error for @p target, which @p field names in the
    /// message ("advances_to" or "auto_advance_to").
    void check_target(const Quest &quest, const Stage &stage, std::string_view field, std::string_view target,
                      std::vector<std::string> &errors) {
      if (quest.find_stage(target) == nullptr)
        errors.push_back(
            std::format(R"("{}": stage "{}" {} unknown stage "{}")", quest.quest_id, stage.name, field, target));
    }

  } // namespace

  ValidationResult validate(const Quest &quest) {
    ValidationResult result;

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
      // Zero is the "not started" sentinel get_stage() reports, so a non-positive
      // sequence is a stage the runtime can never enter.
      if (stage.sequence <= 0)
        result.errors.push_back(std::format(R"("{}": stage "{}" has sequence {}, which must be positive)",
                                            quest.quest_id, stage.name, stage.sequence));

      // A failed ending must also be resolved: is_complete() reads only that flag, so
      // without it a failure would report as an unfinished quest. Loaders normalize the
      // pair; this catches a quest built or edited in memory.
      if (stage.failed && !stage.resolved)
        result.errors.push_back(
            std::format(R"("{}": stage "{}" is failed but not resolved)", quest.quest_id, stage.name));

      for (const auto &target : stage.advances_to)
        check_target(quest, stage, "advances_to", target, result.errors);
      const std::optional<std::string> auto_target = stage.auto_advance_to;
      if (auto_target.has_value())
        check_target(quest, stage, "auto_advance_to", *auto_target, result.errors);
    }

    for (std::size_t i = 1; i < quest.stages.size(); ++i) {
      const Stage &previous = quest.stages[i - 1];
      const Stage &current = quest.stages[i];
      if (current.sequence <= previous.sequence)
        result.warnings.push_back(std::format("\"{}\": stage order \"{}\" (seq {}) is not after \"{}\" (seq {}) — "
                                              "sequence order does not match stage order",
                                              quest.quest_id, current.name, current.sequence, previous.name,
                                              previous.sequence));
    }

    return result;
  }

} // namespace corundum::quest
