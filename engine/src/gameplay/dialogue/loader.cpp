#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/loader.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::gameplay::dialogue {

  // ── File-local error type ────────────────────────────────────────────────────

  struct LoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  // ── Internal helpers ─────────────────────────────────────────────────────────

  template <typename T> static T require(const json &obj, const std::string &key, const std::string &ctx) {
    if (!obj.contains(key))
      throw LoadError(std::format("[{}] missing required field \"{}\"", ctx, key));
    try {
      return obj[key].get<T>();
    } catch (const json::type_error &e) {
      throw LoadError(std::format("[{}] field \"{}\" has wrong type: {}", ctx, key, e.what()));
    }
  }

  static NodeType parse_type(const std::string &raw, const std::string &node_id) {
    if (raw == "talk")
      return NodeType::Talk;
    if (raw == "choice")
      return NodeType::Choice;
    if (raw == "event")
      return NodeType::Event;
    if (raw == "end")
      return NodeType::End;
    throw LoadError(std::format("[{}] unknown node type \"{}\" (expected talk/choice/event/end)", node_id, raw));
  }

  static SequenceMode parse_sequence(const std::string &raw, const std::string &ctx) {
    if (raw == "none")
      return SequenceMode::None;
    if (raw == "once")
      return SequenceMode::Once;
    if (raw == "cycle")
      return SequenceMode::Cycle;
    if (raw == "random")
      return SequenceMode::Random;
    throw LoadError(std::format("[{}] unknown sequence \"{}\" (expected none/once/cycle/random)", ctx, raw));
  }

  // Validate each action string by attempting to parse it at load time.
  static void validate_actions(const json &arr, const std::string &ctx) {
    for (std::size_t i = 0; i < arr.size(); ++i) {
      if (!arr[i].is_string())
        throw LoadError(std::format("[{}] action[{}] must be a string", ctx, i));
      const auto src = arr[i].get<std::string>();
      const auto result = parse_action(src);
      if (!result.has_value())
        throw LoadError(std::format("[{}] action[{}] invalid: {}", ctx, i, result.error().message));
    }
  }

  static Node parse_node(const json &j, const std::string &graph_id) {
    if (!j.contains("id") || !j["id"].is_string())
      throw LoadError(std::format("[{}] a node is missing a string \"id\"", graph_id));

    const std::string node_id = j["id"].get<std::string>();
    if (node_id.empty())
      throw LoadError(std::format("[{}] a node has an empty \"id\"", graph_id));

    const auto type_str = require<std::string>(j, "type", node_id);
    const NodeType type = parse_type(type_str, node_id);

    Node node;
    node.id = node_id;
    node.type = type;

    if (type == NodeType::Talk) {
      node.text = require<std::string>(j, "text", node_id);
      node.next_id = require<std::string>(j, "next", node_id);

      if (node.text.empty())
        throw LoadError(std::format("[{}] \"text\" must not be empty", node_id));
      if (node.next_id.empty())
        throw LoadError(std::format("[{}] \"next\" must not be empty", node_id));
    }

    if (type == NodeType::Event) {
      node.next_id = require<std::string>(j, "next", node_id);
      if (node.next_id.empty())
        throw LoadError(std::format("[{}] \"next\" must not be empty", node_id));

      if (!j.contains("actions") || !j["actions"].is_array())
        throw LoadError(std::format("[{}] event node missing \"actions\" array", node_id));
      if (j["actions"].empty())
        throw LoadError(std::format("[{}] event node has empty \"actions\" array", node_id));

      validate_actions(j["actions"], node_id);
      for (const auto &a : j["actions"])
        node.actions.push_back(a.get<std::string>());
    }

    if (type == NodeType::Choice) {
      if (!j.contains("choices") || !j["choices"].is_array())
        throw LoadError(std::format("[{}] choice node missing \"choices\" array", node_id));
      if (j["choices"].empty())
        throw LoadError(std::format("[{}] choice node has empty \"choices\" array", node_id));

      const auto &arr = j["choices"];
      for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string edge_ctx = std::format("{}:choice[{}]", node_id, i);
        ChoiceEdge edge;

        edge.label = require<std::string>(arr[i], "label", edge_ctx);
        edge.target_id = require<std::string>(arr[i], "target", edge_ctx);

        if (edge.label.empty())
          throw LoadError(std::format("[{}] \"label\" must not be empty", edge_ctx));
        if (edge.target_id.empty())
          throw LoadError(std::format("[{}] \"target\" must not be empty", edge_ctx));

        if (arr[i].contains("condition")) {
          const auto cond = arr[i]["condition"].get<std::string>();
          if (cond.empty())
            throw LoadError(std::format("[{}] \"condition\" must not be empty if present", edge_ctx));
          edge.condition = cond;
        }

        if (arr[i].contains("actions")) {
          if (!arr[i]["actions"].is_array())
            throw LoadError(std::format("[{}] \"actions\" must be an array", edge_ctx));
          validate_actions(arr[i]["actions"], edge_ctx);
          for (const auto &a : arr[i]["actions"])
            edge.actions.push_back(a.get<std::string>());
        }

        if (arr[i].contains("sequence")) {
          const auto seq = arr[i]["sequence"].get<std::string>();
          edge.sequence = parse_sequence(seq, edge_ctx);
        }

        if (arr[i].contains("min_visits")) {
          const int v = arr[i]["min_visits"].get<int>();
          if (v < 1)
            throw LoadError(std::format("[{}] \"min_visits\" must be >= 1", edge_ctx));
          edge.min_visits = v;
        }

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

  static void validate_edges(const Graph &graph) {
    for (const auto &node : graph.nodes) {
      auto check = [&](const std::string &target, const std::string &ctx) {
        if (target == ending_node)
          return;
        if (!graph.id_to_index.contains(target))
          throw LoadError(
              std::format(R"([{}] edge target "{}" does not exist in graph "{}")", ctx, target, graph.graph_id));
      };

      if (node.type == NodeType::Talk || node.type == NodeType::Event)
        check(node.next_id, node.id);

      if (node.type == NodeType::Choice)
        for (std::size_t i = 0; i < node.choices.size(); ++i)
          check(node.choices[i].target_id, std::format("{}:choice[{}]", node.id, i));
    }
  }

  static Graph load_graph_impl(const std::string &path) {
    std::ifstream file(path);
    file.exceptions(std::ios_base::failbit | std::ios_base::badbit);
    if (!file.is_open())
      throw LoadError(std::format("cannot open dialogue file: {}", path));

    const json root = json::parse(file);

    if (!root.contains("id") || !root["id"].is_string())
      throw LoadError(std::format(R"(dialogue file "{}" missing top-level string "id")", path));

    Graph graph;
    graph.graph_id = root["id"].get<std::string>();
    if (graph.graph_id.empty())
      throw LoadError(std::format(R"(dialogue file "{}" has empty top-level "id")", path));

    if (root.contains("speaker") && root["speaker"].is_string())
      graph.speaker = root["speaker"].get<std::string>();

    if (!root.contains("nodes") || !root["nodes"].is_array())
      throw LoadError(std::format("[{}] missing top-level \"nodes\" array", graph.graph_id));
    if (root["nodes"].empty())
      throw LoadError(std::format("[{}] \"nodes\" array is empty", graph.graph_id));

    // Optional graph-level variable defaults
    if (root.contains("variables") && root["variables"].is_object()) {
      for (const auto &[k, v] : root["variables"].items()) {
        if (v.is_number_integer())
          graph.variables.emplace(k, v.get<int>());
        else if (v.is_boolean())
          graph.variables.emplace(k, v.get<bool>() ? 1 : 0);
      }
    }

    graph.nodes.reserve(root["nodes"].size());
    for (const auto &j : root["nodes"])
      graph.nodes.push_back(parse_node(j, graph.graph_id));

    // C++23: flat_map — build index as sorted pairs then construct with sorted_unique tag.
    // Individual flat_map insertions are O(n) each (element shifting); batch construction
    // is O(n log n) total. Duplicates are detected in the sorted order before construction.
    std::vector<std::pair<std::string, std::size_t>> idx_pairs;
    idx_pairs.reserve(graph.nodes.size());
    for (std::size_t i = 0; i < graph.nodes.size(); ++i)
      idx_pairs.emplace_back(graph.nodes[i].id, i);

    // C++23: std::ranges::sort — sort by key for duplicate detection and flat_map construction
    std::ranges::sort(idx_pairs, {}, &std::pair<std::string, std::size_t>::first);

    // C++23: std::ranges::adjacent_find — O(n) duplicate scan on sorted sequence
    auto dup = std::ranges::adjacent_find(idx_pairs, std::ranges::equal_to{},
                                          [](const auto &p) -> const std::string & { return p.first; });
    if (dup != idx_pairs.end())
      throw LoadError(std::format("[{}] duplicate node id \"{}\"", graph.graph_id, dup->first));

    // sorted_unique constructor: O(n) — no further sorting or shifting needed
    graph.id_to_index = std::flat_map<std::string, std::size_t>(std::sorted_unique, idx_pairs.begin(), idx_pairs.end());

    validate_edges(graph);
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
