#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/quest/registry.hpp>

#include <string>
#include <vector>

namespace corundum::gameplay::dialogue {

  /**
   * @brief Cross-check quest_start/quest_advance references in @p graph against @p quests.
   *
   * Covers both node-level actions and choice-edge actions. Messages are node-scoped
   * (e.g. "node 'greet': quest_start references unknown quest 'find_sword'") — callers
   * that iterate multiple graphs should prefix the graph id.
   *
   * @param graph  A loaded dialogue graph.
   * @param quests Registry to validate quest ids and stage names against.
   * @return Error strings; empty if all references resolve.
   */
  [[nodiscard]] std::vector<std::string> validate_quest_refs(const Graph &graph, const quest::Registry &quests);

  /**
   * @brief Cross-check quest_is_started/quest_is_resolved/quest_is_failed/quest_is_at
   * references inside choice-edge condition expressions against @p quests.
   *
   * This is a static token scan, not a full expression parse — it locates quest-helper
   * calls in condition strings and verifies the quest id (and, for quest_is_at, the
   * stage name) exist. Messages are node-scoped, matching validate_quest_refs.
   *
   * @param graph  A loaded dialogue graph.
   * @param quests Registry to validate quest ids and stage names against.
   * @return Error strings; empty if all references resolve.
   */
  [[nodiscard]] std::vector<std::string> validate_condition_quest_refs(const Graph &graph,
                                                                       const quest::Registry &quests);

} // namespace corundum::gameplay::dialogue
