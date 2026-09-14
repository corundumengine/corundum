// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/loader.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::dialogue {

  namespace {

    /// Walks the Event chain from @p start and returns the node that closes a cycle,
    /// or nullptr when the chain is acyclic (or reaches the end).
    const Node *find_event_cycle(const Graph &graph, const Node &start) {
      std::vector<std::string_view> visited;
      const Node *current = &start;
      while (current != nullptr && current->type == NodeType::Event && current->next_id != k_ending_node) {
        if (std::ranges::contains(visited, std::string_view{current->id}))
          return current;
        visited.push_back(current->id);
        current = graph.find(current->next_id);
      }
      return nullptr;
    }

    /// Collects the edge-target errors for one node.
    void check_node_edges(const Graph &graph, const Node &node, std::vector<std::string> &errors) {
      const auto check = [&](const std::string &target, const std::string &context) {
        if (target == k_ending_node)
          return;
        if (!graph.id_to_index.contains(target))
          errors.push_back(
              std::format(R"([{}] edge target "{}" does not exist in graph "{}")", context, target, graph.graph_id));
      };

      if (node.type == NodeType::Talk || node.type == NodeType::Event)
        check(node.next_id, node.id);

      if (node.type == NodeType::Choice)
        for (std::size_t i = 0; i < node.choices.size(); ++i)
          check(node.choices[i].target_id, std::format("{}:choice[{}]", node.id, i));
    }

  } // namespace

  std::vector<std::string> validate_graph(const Graph &graph) {
    std::vector<std::string> errors;

    for (const Node &node : graph.nodes)
      check_node_edges(graph, node, errors);

    for (const Node &start : graph.nodes) {
      if (start.type != NodeType::Event || start.next_id == k_ending_node)
        continue;
      if (find_event_cycle(graph, start) != nullptr) {
        errors.push_back(std::format("[{}] event-node cycle detected: '{}' is reachable from itself "
                                     "through a chain of Event nodes",
                                     graph.graph_id, start.id));
        return errors;
      }
    }

    return errors;
  }

} // namespace corundum::dialogue
