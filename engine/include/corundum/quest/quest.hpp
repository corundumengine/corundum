// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/dialogue/compiled_expr.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::quest {

  /** @brief Current on-disk quest format version. Absent field == version 1. */
  constexpr int k_quest_schema_version = 1;

  /** @brief A single task shown in the journal while its stage is active. */
  struct Objective {
    /**
     * @brief Optional compiled expression that auto-checks this objective.
     *
     * Compiled once at load; quest loaders reject expressions that do not
     * compile. An absent condition means the objective is journal-display only.
     */
    std::optional<dialogue::CompiledExpr> done_condition = std::nullopt;

    /** @brief Journal text displayed for this objective. */
    std::string text{};
  };

  /** @brief A point in a quest's progress, keyed by a sequence integer in FlagStore. */
  struct Stage {
    /**
     * @brief Names of stages this stage may legally advance to.
     *
     * Empty (the default) preserves legacy behaviour: any stage name is an
     * acceptable advance target. When non-empty, `quest::validate` rejects
     * an unknown target name, and `quest::advance` prints a debug-build
     * warning on a transition to a stage not listed here (a transition to
     * the stage's own `auto_advance_to` is always exempt). The warning does
     * not block the transition — `advances_to` is advisory, not enforced.
     */
    std::vector<std::string> advances_to{};

    /**
     * @brief Optional target stage name for objective-driven auto-advance.
     *
     * When set, `quest::tick_quests` advances to this stage once every
     * objective carrying a `done_condition` evaluates true (order-independent).
     * A stage without conditioned objectives never auto-advances. Stages with
     * no `auto_advance_to` are inert.
     */
    std::optional<std::string> auto_advance_to = std::nullopt;

    /**
     * @brief True if this stage is a failure ending.
     *
     * Implies resolved: the loader normalizes the pair, and `quest::validate`
     * rejects a failed stage left unresolved.
     */
    bool failed{false};

    /** @brief Named identifier used in dialogue actions (e.g. "start", "return", "failed"). */
    std::string name{};

    /** @brief Objectives shown in the journal while this stage is active. */
    std::vector<Objective> objectives{};

    /** @brief True if this stage ends the quest. At least one resolved stage per quest. */
    bool resolved{false};

    /** @brief Positive integer written to quest.{id} when this stage is active. */
    int sequence{0};
  };

  /** @brief A named sequence of stages comprising one quest. */
  struct Quest {
    /** @brief Brief premise shown at the top of the journal entry. */
    std::string description{};

    /** @brief Human-readable name shown in the journal. */
    std::string name{};

    /** @brief Machine-readable identifier used in flag keys and dialogue actions. */
    std::string quest_id{};

    /** @brief On-disk format version; 1 for legacy files without the field. */
    int schema_version{k_quest_schema_version};

    /** @brief Ordered list of stages; the last stage is typically the completion sentinel. */
    std::vector<Stage> stages{};

    /**
     * @brief Look up a stage by name.
     * @param stage_name The name field of the desired stage.
     * @return Pointer to the matching stage, or nullptr if not found.
     * @note Linear scan is intentional — stage counts are single-digit in
     *       typical usage. See dialogue::Graph::find for the indexed pattern
     *       used where N is larger.
     */
    [[nodiscard]] const Stage *find_stage(std::string_view stage_name) const noexcept {
      for (const auto &stage : stages) {
        if (stage.name == stage_name)
          return &stage;
      }
      return nullptr;
    }
  };

  /** @brief Diagnostics produced by quest::validate. */
  struct ValidationResult {
    /** @brief One message per violated rule; empty means the quest is valid. */
    std::vector<std::string> errors{};

    /**
     * @brief Non-fatal diagnostics, e.g. a stage-vector order that does not match the
     *        sequence integers (a stage-list reorder without a sequence bump).
     */
    std::vector<std::string> warnings{};

    /** @brief True when no rule was violated, i.e. `errors` is empty. */
    [[nodiscard]] bool ok() const noexcept {
      return errors.empty();
    }
  };

  /** @brief Validate a Quest's stage-uniqueness, sequence, and resolution invariants.
   *
   *  Rejects an empty quest id, quest name, or stage name, duplicate stage names
   *  or sequences, a non-positive stage sequence, a failed stage that is not also
   *  resolved, a quest with no resolved stage, and an `advances_to` /
   *  `auto_advance_to` target naming no stage. Quest loaders run this on every
   *  parsed quest, so it is also the check that catches a quest built or edited
   *  in memory.
   *
   *  @param quest The quest to validate.
   *  @return One message per violated rule in `errors`, plus non-fatal
   *          diagnostics in `warnings`. */
  [[nodiscard]] ValidationResult validate(const Quest &quest);

} // namespace corundum::quest
