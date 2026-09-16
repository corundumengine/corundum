// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/quest/quest.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace corundum::quest {

  std::vector<std::string> validate(const Quest &quest, std::vector<std::string> *warnings) {
    std::vector<std::string> errors;

    auto names = quest.stages | std::views::transform(&Stage::name) | std::ranges::to<std::vector>();
    std::ranges::sort(names);
    const auto dup_name = std::ranges::adjacent_find(names);
    if (dup_name != names.end())
      errors.push_back(std::format("duplicate stage name \"{}\"", *dup_name));

    auto seqs = quest.stages | std::views::transform(&Stage::sequence) | std::ranges::to<std::vector>();
    std::ranges::sort(seqs);
    const auto dup_seq = std::ranges::adjacent_find(seqs);
    if (dup_seq != seqs.end())
      errors.push_back(std::format("duplicate sequence {} in stages", *dup_seq));

    if (!std::ranges::any_of(quest.stages, &Stage::resolved))
      errors.push_back(std::format("\"{}\" has no resolved stage", quest.quest_id));

    for (const auto &stage : quest.stages) {
      // Zero is the "not started" sentinel get_stage() reports, so a non-positive
      // sequence is a stage the runtime can never enter.
      if (stage.sequence <= 0)
        errors.push_back(std::format(R"("{}": stage "{}" has sequence {}, which must be positive)", quest.quest_id,
                                     stage.name, stage.sequence));

      // Stage documents failed as implying resolved; lifecycle() relies on it, while
      // is_complete() reads only `resolved`. Loaders normalize the flag pair, so this
      // catches a quest built or edited in memory.
      if (stage.failed && !stage.resolved)
        errors.push_back(std::format(R"("{}": stage "{}" is failed but not resolved)", quest.quest_id, stage.name));

      for (const auto &target : stage.advances_to) {
        if (quest.find_stage(target) == nullptr)
          errors.push_back(
              std::format(R"("{}": stage "{}" advances_to unknown stage "{}")", quest.quest_id, stage.name, target));
      }
      const auto target = stage.auto_advance_to;
      if (!target.has_value())
        continue;
      if (quest.find_stage(*target) == nullptr)
        errors.push_back(
            std::format(R"("{}": stage "{}" auto_advance_to unknown stage "{}")", quest.quest_id, stage.name, *target));
    }

    if (warnings != nullptr) {
      for (std::size_t i = 1; i < quest.stages.size(); ++i) {
        const auto &prev = quest.stages[i - 1];
        const auto &curr = quest.stages[i];
        if (curr.sequence <= prev.sequence)
          warnings->push_back(std::format("\"{}\": stage order \"{}\" (seq {}) is not after \"{}\" (seq {}) — "
                                          "sequence order does not match stage order",
                                          quest.quest_id, curr.name, curr.sequence, prev.name, prev.sequence));
      }
    }

    return errors;
  }

} // namespace corundum::quest
