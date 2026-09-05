#pragma once

#include <corundum/dialogue/dialogue.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/quest/registry.hpp>

#include <string>
#include <vector>

namespace corundum::dialogue {

  class Registry;

  /**
   * @brief Cross-check quest_start/quest_advance, give_item/take_item, and
   * goto_graph references in @p graph against @p quests, @p items, and @p graphs.
   *
   * Covers both node-level actions and choice-edge actions. Messages are node-scoped
   * (e.g. "node 'greet': quest_start references unknown quest 'find_sword'") — callers
   * that iterate multiple graphs should prefix the graph id.
   *
   * @param graph  A loaded dialogue graph.
   * @param quests Registry to validate quest ids and stage names against.
   * @param items  Registry to validate item ids against; null skips item checks.
   * @param graphs Registry to validate goto_graph targets against; null skips
   *               divert checks (useful when validating a single graph in a tool).
   * @return Error strings; empty if all references resolve.
   */
  [[nodiscard]] std::vector<std::string> validate_quest_refs(const Graph &graph, const quest::Registry &quests,
                                                             const item::Registry *items = nullptr,
                                                             const Registry *graphs = nullptr);

  /**
   * @brief Cross-check quest_is_started/quest_is_resolved/quest_is_failed/quest_is_at
   * references inside choice-edge condition expressions against @p quests.
   *
   * Operates on the refs() collected by each condition's compiled expression —
   * the same grammar used at load, so there is no second parser to drift from
   * the real one. Messages are node-scoped, matching validate_quest_refs.
   *
   * @param graph  A loaded dialogue graph.
   * @param quests Registry to validate quest ids and stage names against.
   * @return Error strings; empty if all references resolve.
   */
  [[nodiscard]] std::vector<std::string> validate_condition_quest_refs(const Graph &graph,
                                                                       const quest::Registry &quests);

} // namespace corundum::dialogue
