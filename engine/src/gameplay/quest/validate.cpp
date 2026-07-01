#include <corundum/gameplay/quest/quest.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <vector>

namespace corundum::gameplay::quest {

  std::vector<std::string> validate(const Quest &quest) {
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

    return errors;
  }

} // namespace corundum::gameplay::quest
