#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/flags.hpp>

#include <format>
#include <string_view>

namespace corundum::gameplay::quest {
  class Registry;
}

// Pure query functions over a dialogue Graph.

namespace corundum::gameplay::dialogue {

  // ── Internal key helpers ──────────────────────────────────────────────────────
  //
  // Keys are prefixed with '_' to prevent collision with author-defined variables.

  // Key set when a Once-sequence edge is traversed. Unique per graph + node + edge.
  [[nodiscard]] inline std::string once_flag_key(std::string_view graph_id, std::string_view node_id,
                                                 std::size_t edge_index) {
    return std::format("_once_{}_{}_{}", graph_id, node_id, edge_index);
  }

  // Key incremented each time the system enters a node. Unique per graph + node.
  [[nodiscard]] inline std::string visit_flag_key(std::string_view graph_id, std::string_view node_id) {
    return std::format("_visit_{}_{}", graph_id, node_id);
  }

  // ── Query functions ───────────────────────────────────────────────────────────

  /**
   * @brief Find a node by id.
   * @return Pointer to the node, or nullptr. O(1) via the pre-built index map.
   */
  [[nodiscard]]
  const Node *find_node(const Graph &graph, const std::string &id) noexcept;

  /**
   * @brief Advance to the next node.
   *
   * Talk / Event — choice_index is ignored; follows node.next_id.
   * Choice       — follows choices[choice_index].target_id.
   *                choice_index is an index into the FULL choices vector.
   *                Returns nullptr if out of range.
   * End          — always returns nullptr (conversation is over).
   *
   * @return Pointer to the next node, or nullptr on end/failure.
   */
  [[nodiscard]]
  const Node *advance(const Graph &graph, const Node &node, int choice_index = 0) noexcept;

  /**
   * @brief Returns the indices into node.choices of choices currently visible.
   *
   * A choice is visible when ALL conditions hold:
   *   - condition is absent/empty, or the expression evaluates to true
   *   - sequence == None, or sequencing logic allows it for this visit
   *   - min_visits is absent, or the node's visit count >= min_visits
   *
   * Sequencing rules:
   *   Once   — hidden after the edge has been taken (once-key set in flags).
   *   Cycle  — the N Cycle choices on this node rotate; only one is shown per
   *            visit (determined by visit_count % N).
   *   Random — one of the Random choices is shown per visit, selected via a
   *            deterministic hash of (graph_id, node_id, visit_count).
   *
   * Bad condition expressions are treated as false (choice hidden).
   * Always returns an empty vector for non-Choice nodes.
   *
   * @param node     The Choice node to evaluate.
   * @param flags    Active FlagStore used for condition and visit-count checks.
   * @param graph_id Owning graph's id, used to construct internal flag keys.
   */
  [[nodiscard]]
  std::vector<std::size_t> visible_choices(const Node &node, const corundum::gameplay::FlagStore &flags,
                                           std::string_view graph_id, const quest::Registry *quests = nullptr);

  /**
   * @brief Returns true if this node terminates the conversation.
   */
  [[nodiscard]]
  bool is_terminal(const Node &node) noexcept;

} // namespace corundum::gameplay::dialogue
