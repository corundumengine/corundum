#include <corundum/dialogue/expr.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>

namespace corundum::quest {

  Lifecycle lifecycle(const Quest &quest, const corundum::world::FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return Lifecycle::NotStarted;
    for (const auto &stage : quest.stages) {
      if (stage.sequence == stage_seq) {
        if (stage.failed)
          return Lifecycle::Failed;
        if (stage.resolved)
          return Lifecycle::Completed;
      }
    }
    return Lifecycle::Active;
  }

  const Stage *current_stage(const Quest &quest, const corundum::world::FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return nullptr;
    for (const auto &stage : quest.stages) {
      if (stage.sequence == stage_seq)
        return &stage;
    }
    return nullptr;
  }

  std::vector<ObjectiveView> objectives(const Quest &quest, const corundum::world::FlagStore &flags,
                                        const Registry *quests) {
    const auto *stage = current_stage(quest, flags);
    if (stage == nullptr)
      return {};

    std::vector<ObjectiveView> result;
    result.reserve(stage->objectives.size());
    for (const auto &obj : stage->objectives) {
      bool done = false;
      if (obj.done_condition.has_value()) {
        const auto eval = dialogue::eval_condition(*obj.done_condition, flags, quests);
        done = eval.has_value() && *eval;
      }
      result.push_back(ObjectiveView{.text = obj.text, .done = done, .has_condition = obj.done_condition.has_value()});
    }
    return result;
  }

} // namespace corundum::quest