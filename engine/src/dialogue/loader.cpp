// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_schema.hpp>
#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/loader.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <format>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::dialogue {

  namespace {

    // ── File-local error type ──────────────────────────────────────────────────

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    /// Parse the optional "schema_version" field. Absent -> legacy version 1.
    std::expected<int, std::string> parse_schema_version(const json &j, const std::string &path) {
      if (!j.contains("schema_version"))
        return 1;
      if (!j["schema_version"].is_number_integer())
        return std::unexpected(std::format("Dialogue '{}' field 'schema_version' must be an integer", path));
      return j["schema_version"].get<int>();
    }

    /// Migrates a dialogue graph JSON object in place from @p from_version up to
    /// k_dialogue_schema_version. No migrations exist yet — schema_version 1 is both
    /// the legacy (absent-field) format and the current format, so this is a no-op
    /// today. Future steps are appended in order and never edited once shipped.
    std::expected<void, std::string> migrate_graph_json(json & /*j*/, int from_version, const std::string &path) {
      if (from_version < 1)
        return std::unexpected(std::format("Dialogue '{}' has invalid schema_version {}", path, from_version));
      return {};
    }

    // ── Internal helpers ───────────────────────────────────────────────────────

    NodeType parse_type(const std::string &raw) {
      if (raw == "talk")
        return NodeType::Talk;
      if (raw == "choice")
        return NodeType::Choice;
      if (raw == "event")
        return NodeType::Event;
      // "end" — handled by the caller after parse_node
      return NodeType::End;
    }

    SequenceMode parse_sequence(const std::string &raw) {
      if (raw == "none")
        return SequenceMode::None;
      if (raw == "once")
        return SequenceMode::Once;
      if (raw == "cycle")
        return SequenceMode::Cycle;
      // "random" — checked by schema
      return SequenceMode::Random;
    }

    void validate_actions(const json &arr, const std::string &context) {
      for (std::size_t i = 0; i < arr.size(); ++i) {
        const std::string source = arr[i].get<std::string>();
        const auto result = parse_action(source);
        if (!result.has_value())
          throw LoadError(std::format("[{}] action[{}] invalid: {}", context, i, result.error().message));
      }
    }

    /// Parse one choice edge; @p context names it in error messages.
    ChoiceEdge parse_choice_edge(const json &j, const std::string &context) {
      ChoiceEdge edge;

      // Schema guarantees: label and target are present and non-empty.
      edge.label = j["label"].get<std::string>();
      edge.target_id = j["target"].get<std::string>();

      if (j.contains("condition")) {
        const std::string condition = j["condition"].get<std::string>();
        if (condition.empty())
          throw LoadError(std::format("[{}] \"condition\" must not be empty if present", context));
        auto compiled = compile(condition);
        if (!compiled)
          throw LoadError(std::format("[{}] condition invalid: {}", context, compiled.error().message));
        edge.condition = std::move(*compiled);
      }

      if (j.contains("actions")) {
        validate_actions(j["actions"], context);
        for (const auto &action : j["actions"])
          edge.actions.push_back(action.get<std::string>());
      }

      if (j.contains("sequence"))
        edge.sequence = parse_sequence(j["sequence"].get<std::string>());

      if (j.contains("min_visits"))
        edge.min_visits = j["min_visits"].get<int>();

      return edge;
    }

    /// Parse the optional "metadata" object; non-string values are ignored.
    std::flat_map<std::string, std::string> parse_metadata(const json &j) {
      std::flat_map<std::string, std::string> metadata;
      if (j.contains("metadata") && j["metadata"].is_object()) {
        for (const auto &[key, value] : j["metadata"].items()) {
          if (value.is_string())
            metadata.emplace(key, value.get<std::string>());
        }
      }
      return metadata;
    }

    Node parse_node(const json &j) {
      // Schema guarantees: id is present and non-empty, type is present and valid.
      Node node;
      node.id = j["id"].get<std::string>();
      node.type = parse_type(j["type"].get<std::string>());

      if (j.contains("once"))
        node.once = j["once"].get<bool>();

      if (node.type == NodeType::Talk) {
        // Schema guarantees: text and next are present and non-empty.
        node.text = j["text"].get<std::string>();
        node.next_id = j["next"].get<std::string>();
      }

      if (node.type == NodeType::Event) {
        // Schema guarantees: next and actions are present.
        node.next_id = j["next"].get<std::string>();
        if (j["actions"].empty())
          throw LoadError(std::format("[{}] event node has empty \"actions\" array", node.id));
        validate_actions(j["actions"], node.id);
        for (const auto &action : j["actions"])
          node.actions.push_back(action.get<std::string>());
      }

      if (node.type == NodeType::Choice) {
        // Schema guarantees: choices is present and non-empty.
        const json &choices = j["choices"];
        for (std::size_t i = 0; i < choices.size(); ++i)
          node.choices.push_back(parse_choice_edge(choices[i], std::format("{}:choice[{}]", node.id, i)));
      }

      node.metadata = parse_metadata(j);
      return node;
    }

    /// Build the id→index lookup, rejecting duplicate node ids.
    std::flat_map<std::string, std::size_t> build_id_index(const std::vector<Node> &nodes) {
      std::vector<std::pair<std::string, std::size_t>> index_pairs;
      index_pairs.reserve(nodes.size());
      for (std::size_t i = 0; i < nodes.size(); ++i)
        index_pairs.emplace_back(nodes[i].id, i);

      std::ranges::sort(index_pairs, {}, &std::pair<std::string, std::size_t>::first);

      const auto duplicate = std::ranges::adjacent_find(
          index_pairs, std::ranges::equal_to{}, [](const auto &entry) -> const std::string & { return entry.first; });
      if (duplicate != index_pairs.end())
        throw LoadError(std::format("duplicate node id \"{}\"", duplicate->first));

      // std::sorted_unique is provided by <flat_map>; the include-cleaner mapping
      // misattributes it to a libc++ internal header, so suppress that one check.
      // NOLINTNEXTLINE(misc-include-cleaner)
      return {std::sorted_unique, index_pairs.begin(), index_pairs.end()};
    }

    /// Parse the optional graph-level "variables" defaults.
    std::flat_map<std::string, int> parse_variables(const json &j) {
      std::flat_map<std::string, int> variables;
      if (j.contains("variables") && j["variables"].is_object()) {
        for (const auto &[key, value] : j["variables"].items()) {
          if (value.is_number_integer())
            variables.emplace(key, value.get<int>());
          else if (value.is_boolean())
            variables.emplace(key, value.get<bool>() ? 1 : 0);
        }
      }
      return variables;
    }

    /// A graph JSON that has been parsed, migrated, and schema-validated.
    struct ValidatedRoot {
      json root;
      int schema_version = k_dialogue_schema_version;
    };

    /// Read @p path and return its schema-validated root object.
    ValidatedRoot load_validated_root(const std::string &path) {
      std::ifstream file(path);
      if (!file)
        throw LoadError(std::format("cannot open dialogue file: {}", path));

      json root = [&file, &path] {
        try {
          return json::parse(file, nullptr, true, true);
        } catch (const json::exception &e) {
          throw LoadError(std::format("malformed JSON in {}: {}", path, e.what()));
        }
      }();

      // Schema version is read before validation so migrations run first.
      auto version_result = parse_schema_version(root, path);
      if (!version_result)
        throw LoadError(std::move(version_result).error());
      const int schema_version = *version_result;
      if (schema_version > k_dialogue_schema_version)
        throw LoadError(std::format("Dialogue '{}' has schema_version {}, newer than this engine supports (max {}) — "
                                    "update the engine",
                                    path, schema_version, k_dialogue_schema_version));
      if (schema_version < k_dialogue_schema_version) {
        auto migration = migrate_graph_json(root, schema_version, path);
        if (!migration)
          throw LoadError(std::move(migration).error());
      }

      auto validated = core::schema_catalog().dialogue_graph_schema().validate(root);
      if (!validated)
        throw LoadError(std::format("[schema] {}: {}", path, validated.error()));

      // The "type" field is optional — directory context already tells us the type.
      if (root.contains("type") && root["type"].is_string() && root["type"] != "graph" && root["type"] != "dialogue")
        std::println(stderr, R"([warning] dialogue file {} has type "{}" instead of "graph")", path,
                     root["type"].get<std::string>());

      return ValidatedRoot{.root = std::move(root), .schema_version = schema_version};
    }

    Graph load_graph_impl(const std::string &path) {
      const ValidatedRoot loaded = load_validated_root(path);
      const json &root = loaded.root;

      // Schema guarantees: id is present and non-empty.
      Graph graph;
      graph.schema_version = loaded.schema_version;
      graph.graph_id = root["id"].get<std::string>();

      if (root.contains("speaker") && root["speaker"].is_string())
        graph.speaker = root["speaker"].get<std::string>();

      if (root.contains("actor_id") && root["actor_id"].is_string())
        graph.actor_id = root["actor_id"].get<std::string>();

      // Schema guarantees: nodes is a non-empty array.
      const json &nodes = root["nodes"];
      graph.nodes.reserve(nodes.size());
      for (const json &node_json : nodes)
        graph.nodes.push_back(parse_node(node_json));

      graph.id_to_index = build_id_index(graph.nodes);
      graph.variables = parse_variables(root);

      const std::vector<std::string> errors = validate_graph(graph);
      if (!errors.empty())
        throw LoadError(errors[0]);

      return graph;
    }

  } // namespace

  // ── Public API ────────────────────────────────────────────────────────────────

  std::expected<Graph, std::string> load_graph(const std::filesystem::path &path) {
    try {
      return load_graph_impl(path.string());
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

} // namespace corundum::dialogue
