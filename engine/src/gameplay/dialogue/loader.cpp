#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/loader.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;

namespace corundum::gameplay::dialogue {

  // ── File-local error type ────────────────────────────────────────────────────

  struct LoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  // ── Internal helpers ─────────────────────────────────────────────────────────

  static NodeType parse_type(const std::string &raw) {
    if (raw == "talk")
      return NodeType::Talk;
    if (raw == "choice")
      return NodeType::Choice;
    if (raw == "event")
      return NodeType::Event;
    // "end" — handled by the caller after parse_node
    return NodeType::End;
  }

  static SequenceMode parse_sequence(const std::string &raw) {
    if (raw == "none")
      return SequenceMode::None;
    if (raw == "once")
      return SequenceMode::Once;
    if (raw == "cycle")
      return SequenceMode::Cycle;
    // "random" — checked by schema
    return SequenceMode::Random;
  }

  static void validate_actions(const json &arr, const std::string &ctx) {
    for (std::size_t i = 0; i < arr.size(); ++i) {
      const auto src = arr[i].get<std::string>();
      const auto result = parse_action(src);
      if (!result.has_value())
        throw LoadError(std::format("[{}] action[{}] invalid: {}", ctx, i, result.error().message));
    }
  }

  static Node parse_node(const json &j) {
    // Schema guarantees: id is present and non-empty, type is present and valid.
    const std::string node_id = j["id"].get<std::string>();
    const std::string type_str = j["type"].get<std::string>();
    const NodeType type = parse_type(type_str);

    Node node;
    node.id = node_id;
    node.type = type;

    if (type == NodeType::Talk) {
      // Schema guarantees: text and next are present and non-empty.
      node.text = j["text"].get<std::string>();
      node.next_id = j["next"].get<std::string>();
    }

    if (type == NodeType::Event) {
      // Schema guarantees: next and actions are present.
      node.next_id = j["next"].get<std::string>();
      if (j["actions"].empty())
        throw LoadError(std::format("[{}] event node has empty \"actions\" array", node_id));
      validate_actions(j["actions"], node_id);
      for (const auto &a : j["actions"])
        node.actions.push_back(a.get<std::string>());
    }

    if (type == NodeType::Choice) {
      // Schema guarantees: choices is present and non-empty.
      const auto &arr = j["choices"];
      for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string edge_ctx = std::format("{}:choice[{}]", node_id, i);
        ChoiceEdge edge;

        // Schema guarantees: label and target are present and non-empty.
        edge.label = arr[i]["label"].get<std::string>();
        edge.target_id = arr[i]["target"].get<std::string>();

        if (arr[i].contains("condition")) {
          const auto cond = arr[i]["condition"].get<std::string>();
          if (cond.empty())
            throw LoadError(std::format("[{}] \"condition\" must not be empty if present", edge_ctx));
          edge.condition = cond;
        }

        if (arr[i].contains("actions")) {
          validate_actions(arr[i]["actions"], edge_ctx);
          for (const auto &a : arr[i]["actions"])
            edge.actions.push_back(a.get<std::string>());
        }

        if (arr[i].contains("sequence"))
          edge.sequence = parse_sequence(arr[i]["sequence"].get<std::string>());

        if (arr[i].contains("min_visits"))
          edge.min_visits = arr[i]["min_visits"].get<int>();

        node.choices.push_back(std::move(edge));
      }
    }

    // Metadata — optional key/value pairs for the presentation layer.
    if (j.contains("metadata") && j["metadata"].is_object()) {
      for (const auto &[k, v] : j["metadata"].items()) {
        if (v.is_string())
          node.metadata.emplace(k, v.get<std::string>());
      }
    }

    return node;
  }

  static Graph load_graph_impl(const std::string &path) {
    std::ifstream file(path);
    if (!file)
      throw LoadError(std::format("cannot open dialogue file: {}", path));

    const json root = [&] {
      try {
        return json::parse(file, nullptr, true, true);
      } catch (const json::parse_error &e) {
        throw LoadError(std::format("malformed JSON in {}: {}", path, e.what()));
      }
    }();

    // ── Schema validation ────────────────────────────────────────────────────
    {
      auto sv = core::dialogue_graph_schema().validate(root);
      if (!sv)
        throw LoadError(std::format("[schema] {}", sv.error()));
    }

    // ── Type field (optional — directory context tells us the type) ──────────
    if (root.contains("type") && root["type"].is_string() && root["type"] != "graph")
      std::println(stderr, "[warning] dialogue file {} has type \"{}\" instead of \"graph\"", path,
                   root["type"].get<std::string>());

    // Schema guarantees: id is present and non-empty.
    Graph graph;
    graph.graph_id = root["id"].get<std::string>();

    if (root.contains("speaker") && root["speaker"].is_string())
      graph.speaker = root["speaker"].get<std::string>();

    // Schema guarantees: nodes is a non-empty array.
    const auto &nodes_arr = root["nodes"];
    graph.nodes.reserve(nodes_arr.size());
    for (const auto &node_json : nodes_arr)
      graph.nodes.push_back(parse_node(node_json));

    // C++23: flat_map — build index as sorted pairs then construct with sorted_unique tag.
    std::vector<std::pair<std::string, std::size_t>> idx_pairs;
    idx_pairs.reserve(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
      idx_pairs.emplace_back(graph.nodes[i].id, i);

    std::ranges::sort(idx_pairs, {}, &std::pair<std::string, std::size_t>::first);

    auto dup = std::ranges::adjacent_find(idx_pairs, std::ranges::equal_to{},
                                          [](const auto &p) -> const std::string & { return p.first; });
    if (dup != idx_pairs.end())
      throw LoadError(std::format("[{}] duplicate node id \"{}\"", graph.graph_id, dup->first));

    graph.id_to_index = std::flat_map<std::string, std::size_t>(std::sorted_unique, idx_pairs.begin(), idx_pairs.end());

    // Optional graph-level variable defaults
    if (root.contains("variables") && root["variables"].is_object()) {
      for (const auto &[k, v] : root["variables"].items()) {
        if (v.is_number_integer())
          graph.variables.emplace(k, v.get<int>());
        else if (v.is_boolean())
          graph.variables.emplace(k, v.get<bool>() ? 1 : 0);
      }
    }

    {
      auto errors = validate_graph(graph);
      if (!errors.empty())
        throw LoadError(errors[0]);
    }
    return graph;
  }

  // ── Public API ────────────────────────────────────────────────────────────────

  std::expected<Graph, std::string> load_graph(const std::string &path) {
    try {
      return load_graph_impl(path);
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

} // namespace corundum::gameplay::dialogue
