#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::quest {

  /** @brief A single task shown in the journal while its stage is active. */
  struct Objective {
    /** @brief Journal text displayed for this objective. */
    std::string text;
    /** @brief Optional expression evaluated against FlagStore to auto-check this objective. */
    std::optional<std::string> done_condition;
  };

  /** @brief A point in a quest's progress, keyed by a sequence integer in FlagStore. */
  struct Stage {
    /** @brief Named identifier used in dialogue actions (e.g. "start", "return", "failed"). */
    std::string name;
    /** @brief Positive integer written to quest.{id} when this stage is active. */
    int sequence{0};
    /** @brief True if this stage ends the quest. At least one resolved stage per quest. */
    bool resolved{false};
    /** @brief True if this stage is a failure ending. Implies resolved (loader enforces). */
    bool failed{false};
    /** @brief Objectives shown in the journal while this stage is active. */
    std::vector<Objective> objectives;
  };

  /** @brief A named sequence of stages comprising one quest. */
  struct Quest {
    /** @brief Machine-readable identifier used in flag keys and dialogue actions. */
    std::string quest_id;
    /** @brief Human-readable name shown in the journal. */
    std::string name;
    /** @brief Brief premise shown at the top of the journal entry. */
    std::string description;
    /** @brief Ordered list of stages; the last stage is typically the completion sentinel. */
    std::vector<Stage> stages;

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

  /** @brief Validate stage-uniqueness and resolution invariants on an in-memory Quest.
   *  @param quest The quest to validate.
   *  @return One message per violated rule; empty vector = valid. */
  [[nodiscard]] std::vector<std::string> validate(const Quest &quest);

} // namespace corundum::gameplay::quest
