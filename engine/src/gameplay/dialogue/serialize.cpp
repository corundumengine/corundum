#include <corundum/gameplay/dialogue/serialize.hpp>

namespace corundum::gameplay::dialogue {

  namespace {

    [[nodiscard]] const char *node_type_str(NodeType type) noexcept {
      switch (type) {
      case NodeType::Talk:
        return "talk";
      case NodeType::Choice:
        return "choice";
      case NodeType::Event:
        return "event";
      case NodeType::End:
        return "end";
      }
      return "end";
    }

    [[nodiscard]] const char *sequence_str(SequenceMode mode) noexcept {
      switch (mode) {
      case SequenceMode::Once:
        return "once";
      case SequenceMode::Cycle:
        return "cycle";
      case SequenceMode::Random:
        return "random";
      case SequenceMode::None:
        break;
      }
      return nullptr;
    }

    [[nodiscard]] nlohmann::json serialize_choice(const ChoiceEdge &ch) {
      nlohmann::json cj;
      cj["label"] = ch.label;
      cj["target"] = ch.target_id;
      if (ch.condition.has_value())
        cj["condition"] = *ch.condition;
      if (!ch.actions.empty())
        cj["actions"] = ch.actions;
      if (const auto *seq = sequence_str(ch.sequence))
        cj["sequence"] = seq;
      if (ch.min_visits.has_value())
        cj["min_visits"] = *ch.min_visits;
      return cj;
    }

    [[nodiscard]] nlohmann::json serialize_node(const Node &node) {
      nlohmann::json nj;
      nj["id"] = node.id;
      nj["type"] = node_type_str(node.type);

      switch (node.type) {
      case NodeType::Talk:
        nj["text"] = node.text;
        nj["next"] = node.next_id;
        break;
      case NodeType::Choice: {
        nj["choices"] = nlohmann::json::array();
        for (const auto &ch : node.choices)
          nj["choices"].push_back(serialize_choice(ch));
        break;
      }
      case NodeType::Event:
        nj["next"] = node.next_id;
        nj["actions"] = node.actions;
        break;
      case NodeType::End:
        break;
      }

      if (!node.metadata.empty()) {
        nlohmann::json meta = nlohmann::json::object();
        for (const auto &[k, v] : node.metadata)
          meta[k] = v;
        nj["metadata"] = std::move(meta);
      }

      return nj;
    }

  } // namespace

  nlohmann::json serialize_graph(const Graph &graph) {
    nlohmann::json j;
    j["type"] = "graph";
    j["id"] = graph.graph_id;

    if (!graph.speaker.empty())
      j["speaker"] = graph.speaker;

    if (!graph.variables.empty()) {
      nlohmann::json vars = nlohmann::json::object();
      for (const auto &[k, v] : graph.variables)
        vars[k] = v;
      j["variables"] = std::move(vars);
    }

    j["nodes"] = nlohmann::json::array();
    for (const auto &node : graph.nodes)
      j["nodes"].push_back(serialize_node(node));

    return j;
  }

} // namespace corundum::gameplay::dialogue
