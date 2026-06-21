#include "graph_layout.hpp"
#include "layout.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tools::talesmith {

  void recompute_layout(corundum::gameplay::dialogue::Graph &graph, std::vector<NodeLayout> &layout) {
    layout.clear();
    const auto n = graph.nodes.size();
    if (n == 0)
      return;
    layout.resize(n);

    std::vector<std::unordered_set<std::size_t>> children(n);
    for (std::size_t i = 0; i < n; ++i) {
      const auto &node = graph.nodes[i];
      if (node.type == corundum::gameplay::dialogue::NodeType::Talk ||
          node.type == corundum::gameplay::dialogue::NodeType::Event) {
        if (!node.next_id.empty() && node.next_id != "end") {
          if (auto it = graph.id_to_index.find(node.next_id); it != graph.id_to_index.end())
            children[i].insert(it->second);
        }
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        for (const auto &ch : node.choices) {
          if (auto it = graph.id_to_index.find(ch.target_id); it != graph.id_to_index.end())
            children[i].insert(it->second);
        }
      }
    }

    std::vector<int> layer(n, -1);
    std::vector<std::size_t> queue;
    std::unordered_set<std::size_t> visited;

    queue.push_back(0);
    visited.insert(0);
    layer[0] = 0;
    std::size_t head = 0;
    while (head < queue.size()) {
      const auto cur = queue[head++];
      for (const auto child : children[cur]) {
        if (visited.insert(child).second) {
          layer[child] = layer[cur] + 1;
          queue.push_back(child);
        }
      }
    }

    for (std::size_t i = 0; i < n; ++i) {
      if (!visited.contains(i)) {
        layer[i] = 0;
        for (const auto &[id, idx] : graph.id_to_index) {
          if (idx == i)
            continue;
          if (visited.contains(idx) && children[idx].contains(i))
            layer[i] = std::max(layer[i], layer[idx] + 1);
        }
      }
    }

    int max_layer = 0;
    std::unordered_map<int, std::vector<std::size_t>> layer_nodes;
    for (std::size_t i = 0; i < n; ++i) {
      if (layer[i] < 0)
        layer[i] = 0;
      max_layer = std::max(max_layer, layer[i]);
      layer_nodes[layer[i]].push_back(i);
    }

    for (int l = 0; l <= max_layer; ++l) {
      auto &nodes = layer_nodes[l];
      const auto count = nodes.size();
      const float total_w = count * NODE_W + (count - 1) * NODE_SPACING_X;
      const float start_x = (GRAPH_W - total_w) * 0.5f;
      for (std::size_t i = 0; i < count; ++i) {
        const auto idx = nodes[i];
        layout[idx].layer = l;
        layout[idx].x = start_x + i * (NODE_W + NODE_SPACING_X);
        layout[idx].y = 40.f + l * (NODE_H + NODE_SPACING_Y);
      }
    }
  }

} // namespace tools::talesmith
