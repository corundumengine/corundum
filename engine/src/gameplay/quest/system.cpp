#include <corundum/gameplay/quest/system.hpp>

#include <print>

namespace corundum::gameplay::quest {

  int get_stage(std::string_view quest_id, const FlagStore &flags) noexcept {
    return corundum::gameplay::visit_count(flags, quest_flag_key(quest_id));
  }

  bool is_complete(const Quest &quest, const FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return false;
    for (const auto &stage : quest.stages) {
      if (stage.resolved && stage.sequence == stage_seq)
        return true;
    }
    return false;
  }

  bool is_failed(const Quest &quest, const FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return false;
    for (const auto &stage : quest.stages) {
      if (stage.failed && stage.sequence == stage_seq)
        return true;
    }
    return false;
  }

  void start(const Quest &quest, FlagStore &flags) {
    if (quest.stages.empty())
      return;
    const auto key = quest_flag_key(quest.quest_id);
    if (corundum::gameplay::visit_count(flags, key) > 0)
      return; // already started
    flags[key] = quest.stages[0].sequence;
  }

  void advance(const Quest &quest, std::string_view stage_name, FlagStore &flags) {
    const auto *stage = quest.find_stage(stage_name);
    if (!stage) {
      std::println(stderr, "[quest] advance(\"{}\", \"{}\"): stage not found", quest.quest_id, stage_name);
      return;
    }
    flags[quest_flag_key(quest.quest_id)] = stage->sequence;
  }

  std::vector<const Quest *> active_quests(const Registry &registry, const FlagStore &flags) {
    std::vector<const Quest *> result;
    for (const auto &[id, quest] : registry) {
      if (get_stage(id, flags) > 0)
        result.push_back(&quest);
    }
    return result;
  }

} // namespace corundum::gameplay::quest
