// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/dialogue/compiled_expr.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::quest {

/** Current on-disk quest format version. Absent field == version 1. */
constexpr int k_quest_schema_version = 1;

/** A single task shown in the journal while its stage is active. */
struct Objective {
  /** Optional compiled expression that auto-checks this objective.
   *
   *  Compiled once at load; quest loaders reject expressions that do not
   *  compile. An absent condition means the objective is journal-display only.
   */
  std::optional<dialogue::CompiledExpr> done_condition = std::nullopt;

  /** Journal text displayed for this objective. */
  std::string text{};
};

/** A point in a quest's progress, keyed by a sequence integer in FlagStore. */
struct Stage {
  /** Names of stages this stage may legally advance to.
   *
   *  Empty (the default) means any stage name is an acceptable advance
   *  target. When non-empty, `quest::validate` rejects
   *  an unknown target name, and `quest::advance` prints a debug-build
   *  warning on a transition to a stage not listed here (a transition to
   *  the stage's own `auto_advance_to` is always exempt). The warning does
   *  not block the transition — `advances_to` is advisory, not enforced.
   */
  std::vector<std::string> advances_to{};

  /** Optional target stage name for objective-driven auto-advance.
   *
   *  When set, `quest::tick_quests` advances to this stage once every
   *  objective carrying a `done_condition` evaluates true (order-independent).
   *  A stage without conditioned objectives never auto-advances. Stages with
   *  no `auto_advance_to` are inert. `quest::validate` rejects a self-target
   *  and any cycle in the auto-advance graph, either of which would re-enter
   *  its stages every tick.
   */
  std::optional<std::string> auto_advance_to = std::nullopt;

  /** True if this stage is a failure ending.
   *
   *  Implies resolved: the loader normalizes the pair, and `quest::validate`
   *  rejects a failed stage left unresolved.
   */
  bool failed{false};

  /** Named identifier used in dialogue actions (e.g. "start", "return", "failed"). */
  std::string name{};

  /** Objectives shown in the journal while this stage is active. */
  std::vector<Objective> objectives{};

  /** True if this stage ends the quest. At least one resolved stage per quest. */
  bool resolved{false};

  /** Positive integer written to quest.{id} when this stage is active. */
  int sequence{0};
};

/** A named sequence of stages comprising one quest. */
struct Quest {
  /** Brief premise shown at the top of the journal entry. */
  std::string description{};

  /** Human-readable name shown in the journal. */
  std::string name{};

  /** Machine-readable identifier used in flag keys and dialogue actions. */
  std::string quest_id{};

  /** On-disk format version; a file without the field reads as 1. */
  int schema_version{k_quest_schema_version};

  /** Ordered list of stages; the last stage is typically the completion sentinel. */
  std::vector<Stage> stages{};

  /** Look up a stage by name.
   *  @param stage_name The name field of the desired stage.
   *  @return Pointer to the matching stage, or nullptr if not found.
   *  @note Linear scan is intentional — stage counts are single-digit in
   *        typical usage. See dialogue::Graph::find for the indexed pattern
   *        used where N is larger.
   */
  [[nodiscard]] const Stage *find_stage(std::string_view stage_name) const noexcept {
    for (const auto &stage : stages) {
      if (stage.name == stage_name)
        return &stage;
    }
    return nullptr;
  }
};

/** Diagnostics produced by quest::validate. */
struct ValidationResult {
  /** One message per violated rule; empty means the quest is valid. */
  std::vector<std::string> errors{};

  /** Non-fatal diagnostics, e.g. a stage-vector order that does not match the
   *  sequence integers (a stage-list reorder without a sequence bump).
   */
  std::vector<std::string> warnings{};

  /** True when no rule was violated, i.e. `errors` is empty. */
  [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

/** Validate a Quest's stage-uniqueness, sequence, and resolution invariants.
 *
 *  Rejects an empty quest id, quest name, or stage name, duplicate stage names
 *  or sequences, a non-positive stage sequence, a failed stage that is not also
 *  resolved, a quest with no resolved stage, an `advances_to` / `auto_advance_to`
 *  target naming no stage, and an `auto_advance_to` that targets its own stage or
 *  forms a cycle. Quest loaders run this on every parsed quest, so it is also the
 *  check that catches a quest built or edited in memory.
 *
 *  @param quest The quest to validate.
 *  @return One message per violated rule in `errors`, plus non-fatal
 *          diagnostics in `warnings`.
 */
[[nodiscard]] ValidationResult validate(const Quest &quest);

} // namespace corundum::gameplay::quest
