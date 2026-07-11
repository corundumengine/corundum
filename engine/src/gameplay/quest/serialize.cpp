#include <corundum/gameplay/quest/serialize.hpp>

namespace corundum::gameplay::quest {

  namespace {

    [[nodiscard]] nlohmann::json serialize_objective(const Objective &obj) {
      nlohmann::json oj;
      oj["text"] = obj.text;
      if (obj.done_condition.has_value())
        oj["done_condition"] = *obj.done_condition;
      return oj;
    }

    [[nodiscard]] nlohmann::json serialize_stage(const Stage &s) {
      nlohmann::json sj;
      sj["name"] = s.name;
      sj["sequence"] = s.sequence;
      if (s.resolved)
        sj["resolved"] = true;
      if (s.failed)
        sj["failed"] = true;

      sj["objectives"] = nlohmann::json::array();
      for (const auto &obj : s.objectives)
        sj["objectives"].push_back(serialize_objective(obj));

      return sj;
    }

  } // namespace

  nlohmann::json serialize_quest(const Quest &quest) {
    nlohmann::json j;
    j["type"] = "quest";
    j["id"] = quest.quest_id;
    j["name"] = quest.name;
    j["description"] = quest.description;

    j["stages"] = nlohmann::json::array();
    for (const auto &s : quest.stages)
      j["stages"].push_back(serialize_stage(s));

    return j;
  }

} // namespace corundum::gameplay::quest
