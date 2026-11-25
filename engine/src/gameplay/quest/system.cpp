// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/world/flags.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::quest {

namespace {

/** FlagStore key recording that the player has entered a stage, e.g. quest.{id}.seen.{stage}. */
[[nodiscard]] std::string seen_flag_key(std::string_view quest_id, std::string_view stage_name) {
  return std::format("quest.{}.seen.{}", quest_id, stage_name);
}

/** True when `stage` has at least one objective with a done condition and every such objective is done. */
[[nodiscard]] bool conditioned_objectives_done(const Stage &stage, const world::FlagStore &flags,
                                               const Registry &registry, std::string_view zone_id) {
  const auto has_condition = [](const Objective &objective) { return objective.done_condition.has_value(); };
  const auto is_done = [&](const Objective &objective) {
    return !objective.done_condition.has_value() || objective_done(objective, flags, &registry, zone_id);
  };
  return std::ranges::any_of(stage.objectives, has_condition) && std::ranges::all_of(stage.objectives, is_done);
}

} // namespace

int get_stage(std::string_view quest_id, const world::FlagStore &flags) {
  return world::visit_count(flags, quest_flag_key(quest_id));
}

bool is_resolved(const Quest &quest, const world::FlagStore &flags) {
  const Stage *stage = current_stage(quest, flags);
  return stage != nullptr && stage->resolved;
}

bool is_failed(const Quest &quest, const world::FlagStore &flags) {
  const Stage *stage = current_stage(quest, flags);
  return stage != nullptr && stage->failed;
}

void start(const Quest &quest, world::FlagStore &flags) {
  if (quest.stages.empty()) {
    detail::warn_log(R"([quest] start("{}"): quest has no stages)", quest.quest_id);
    return;
  }
  const int first_sequence = quest.stages[0].sequence;
  // A non-positive sequence collides with the "not started" sentinel, so the quest
  // could never report as started. Only reachable for an unvalidated Registry::add quest.
  if (first_sequence <= 0) {
    detail::warn_log(R"([quest] start("{}"): first stage "{}" has sequence {}, which must be positive)", quest.quest_id,
                     quest.stages[0].name, first_sequence);
    return;
  }
  const auto key = quest_flag_key(quest.quest_id);
  if (world::visit_count(flags, key) > 0)
    return; // already started
  flags[key] = first_sequence;
  world::set_flag(flags, seen_flag_key(quest.quest_id, quest.stages[0].name));
}

void advance(const Quest &quest, std::string_view stage_name, world::FlagStore &flags) {
  const auto *stage = quest.find_stage(stage_name);
  if (stage == nullptr) {
    detail::warn_log(R"([quest] advance("{}", "{}"): stage not found)", quest.quest_id, stage_name);
    return;
  }
#ifndef NDEBUG
  if (const auto *current = current_stage(quest, flags); current != nullptr) {
    const bool auto_target = current->auto_advance_to.has_value() && *current->auto_advance_to == stage_name;
    if (!current->advances_to.empty() && !auto_target && !std::ranges::contains(current->advances_to, stage_name)) {
      detail::warn_log(R"([quest] advance("{}", "{}"): "{}" not listed in stage "{}" advances_to)", quest.quest_id,
                       stage_name, stage_name, current->name);
    }
  }
#endif
  flags[quest_flag_key(quest.quest_id)] = stage->sequence;
  world::set_flag(flags, seen_flag_key(quest.quest_id, stage->name));
}

std::vector<const Quest *> started_quests(const Registry &registry, const world::FlagStore &flags) {
  std::vector<const Quest *> result;
  result.reserve(registry.size());
  for (const auto &[id, quest] : registry) {
    if (get_stage(id, flags) > 0)
      result.push_back(&quest);
  }
  return result;
}

void tick_quests(const Registry &registry, world::FlagStore &flags, std::string_view zone_id) {
  for (const auto &[id, quest] : registry) {
    // Equivalent to lifecycle() == Active in one lookup: a null stage covers "not started"
    // and a dangling sequence. `resolved` also covers `failed` on a validated quest; the
    // explicit `failed` check keeps an unvalidated Registry::add quest from advancing out
    // of a terminal failure.
    const Stage *stage = current_stage(quest, flags);
    if (stage == nullptr || stage->resolved || stage->failed)
      continue;
    if (!stage->auto_advance_to.has_value())
      continue;
    const auto &target = *stage->auto_advance_to;
    // A self-target would re-enter this stage (and re-record its breadcrumb) every tick;
    // validate() rejects it, but Registry::add() skips validation.
    if (target == stage->name)
      continue;

    if (conditioned_objectives_done(*stage, flags, registry, zone_id))
      advance(quest, target, flags);
  }
}

} // namespace corundum::gameplay::quest
