// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::quest {

  namespace {

    /** @brief FlagStore key recording that the player has entered a stage, e.g. quest.{id}.seen.{stage}. */
    [[nodiscard]] std::string seen_flag_key(std::string_view quest_id, std::string_view stage_name) {
      return std::format("quest.{}.seen.{}", quest_id, stage_name);
    }

  } // namespace

  int get_stage(std::string_view quest_id, const corundum::world::FlagStore &flags) noexcept {
    return corundum::world::visit_count(flags, quest_flag_key(quest_id));
  }

  bool is_complete(const Quest &quest, const corundum::world::FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return false;
    for (const auto &stage : quest.stages) {
      if (stage.resolved && stage.sequence == stage_seq)
        return true;
    }
    return false;
  }

  bool is_failed(const Quest &quest, const corundum::world::FlagStore &flags) noexcept {
    const int stage_seq = get_stage(quest.quest_id, flags);
    if (stage_seq <= 0)
      return false;
    for (const auto &stage : quest.stages) {
      if (stage.failed && stage.sequence == stage_seq)
        return true;
    }
    return false;
  }

  void start(const Quest &quest, corundum::world::FlagStore &flags) {
    if (quest.stages.empty())
      return;
    const auto key = quest_flag_key(quest.quest_id);
    if (corundum::world::visit_count(flags, key) > 0)
      return; // already started
    flags[key] = quest.stages[0].sequence;
    corundum::world::set_flag(flags, seen_flag_key(quest.quest_id, quest.stages[0].name));
  }

  void advance(const Quest &quest, std::string_view stage_name, corundum::world::FlagStore &flags) {
    const auto *stage = quest.find_stage(stage_name);
    if (!stage) {
      std::println(stderr, "[quest] advance(\"{}\", \"{}\"): stage not found", quest.quest_id, stage_name);
      return;
    }
#ifndef NDEBUG
    if (const auto *current = current_stage(quest, flags); current != nullptr) {
      const bool auto_target = current->auto_advance_to.has_value() && *current->auto_advance_to == stage_name;
      if (!current->advances_to.empty() && !auto_target && !std::ranges::contains(current->advances_to, stage_name)) {
        std::println(stderr, "[quest] advance(\"{}\", \"{}\"): \"{}\" not listed in stage \"{}\" advances_to",
                     quest.quest_id, stage_name, stage_name, current->name);
      }
    }
#endif
    flags[quest_flag_key(quest.quest_id)] = stage->sequence;
    corundum::world::set_flag(flags, seen_flag_key(quest.quest_id, stage->name));
  }

  std::vector<const Quest *> active_quests(const Registry &registry, const corundum::world::FlagStore &flags) {
    std::vector<const Quest *> result;
    for (const auto &[id, quest] : registry) {
      if (get_stage(id, flags) > 0)
        result.push_back(&quest);
    }
    return result;
  }

  void tick_quests(const Registry &registry, corundum::world::FlagStore &flags, std::string_view zone_id) {
    for (const auto &[id, quest] : registry) {
      if (lifecycle(quest, flags) != Lifecycle::Active)
        continue;
      const auto *stage = current_stage(quest, flags);
      if (stage == nullptr)
        continue;
      if (!stage->auto_advance_to.has_value())
        continue;
      const auto &target = *stage->auto_advance_to;

      bool any_conditioned = false;
      bool all_done = true;
      for (const auto &obj : stage->objectives) {
        if (!obj.done_condition.has_value())
          continue;
        any_conditioned = true;
        if (!corundum::dialogue::evaluate(*obj.done_condition, flags, &registry, {}, zone_id))
          all_done = false;
      }
      if (any_conditioned && all_done)
        advance(quest, target, flags);
    }
  }

} // namespace corundum::quest
