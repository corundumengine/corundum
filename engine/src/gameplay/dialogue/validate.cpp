#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/loader.hpp>

#include <format>
#include <vector>

namespace corundum::gameplay::dialogue {

  std::vector<std::string> validate_graph(const Graph &graph) {
    std::vector<std::string> errors;

    for (const auto &node : graph.nodes) {
      auto check = [&](const std::string &target, const std::string &ctx) {
        if (target == ending_node)
          return;
        if (!graph.id_to_index.contains(target))
          errors.push_back(
              std::format(R"([{}] edge target "{}" does not exist in graph "{}")", ctx, target, graph.graph_id));
      };

      if (node.type == NodeType::Talk || node.type == NodeType::Event)
        check(node.next_id, node.id);

      if (node.type == NodeType::Choice)
        for (std::size_t i = 0; i < node.choices.size(); ++i)
          check(node.choices[i].target_id, std::format("{}:choice[{}]", node.id, i));
    }

    for (const auto &start : graph.nodes) {
      if (start.type != NodeType::Event)
        continue;
      if (start.next_id == ending_node)
        continue;
      std::vector<std::string_view> visited;
      const Node *cur = &start;
      while (cur && cur->type == NodeType::Event && cur->next_id != ending_node) {
        for (const auto &v : visited)
          if (v == cur->id) {
            errors.push_back(std::format("[{}] event-node cycle detected: '{}' is reachable from itself "
                                         "through a chain of Event nodes",
                                         graph.graph_id, start.id));
            return errors;
          }
        visited.push_back(cur->id);
        cur = graph.find(cur->next_id);
      }
    }

    return errors;
  }

} // namespace corundum::gameplay::dialogue
