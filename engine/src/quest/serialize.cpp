// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/quest/quest.hpp>
#include <corundum/quest/serialize.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is
// still required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

namespace corundum::quest {

  namespace {

    [[nodiscard]] nlohmann::json serialize_objective(const Objective &objective) {
      nlohmann::json objective_json;
      objective_json["text"] = objective.text;
      if (objective.done_condition.has_value())
        objective_json["done_condition"] = objective.done_condition->source();
      return objective_json;
    }

    [[nodiscard]] nlohmann::json serialize_stage(const Stage &stage) {
      nlohmann::json stage_json;
      stage_json["name"] = stage.name;
      stage_json["sequence"] = stage.sequence;
      if (stage.resolved)
        stage_json["resolved"] = true;
      if (stage.failed)
        stage_json["failed"] = true;
      if (!stage.advances_to.empty())
        stage_json["advances_to"] = stage.advances_to;
      if (stage.auto_advance_to.has_value())
        stage_json["auto_advance_to"] = *stage.auto_advance_to;

      stage_json["objectives"] = nlohmann::json::array();
      for (const auto &objective : stage.objectives)
        stage_json["objectives"].push_back(serialize_objective(objective));

      return stage_json;
    }

  } // namespace

  nlohmann::json serialize(const Quest &quest) {
    nlohmann::json j;
    j["type"] = "quest";
    j["id"] = quest.quest_id;
    if (quest.schema_version != k_quest_schema_version)
      j["schema_version"] = quest.schema_version;
    j["name"] = quest.name;
    j["description"] = quest.description;

    j["stages"] = nlohmann::json::array();
    for (const auto &stage : quest.stages)
      j["stages"].push_back(serialize_stage(stage));

    return j;
  }

} // namespace corundum::quest
