#pragma once

#include <cstdint>
#include <flat_map> // C++23: flat_map — sorted contiguous storage, replaces unordered_map
#include <optional>
#include <string>
#include <vector>

namespace corundum::gameplay::dialogue {

  /**
   * @brief Classifies the role of a node in the dialogue graph.
   *
   * Talk   — NPC speaks; one outgoing edge (next_id).
   * Choice — Player picks from 1-N labelled edges.
   * Event  — Executes actions silently, no UI; advances to next_id automatically.
   * End    — Conversation terminates; no edges.
   */
  enum class NodeType : uint8_t { Talk, Choice, Event, End };

  /**
   * @brief Controls how often a choice edge is available across repeated visits
   * to the same Choice node.
   *
   * None   — Always available (default).
   * Once   — Available once; hidden after being taken.
   * Cycle  — Rotates with other Cycle choices on this node; one is visible per
   *          visit, cycling round-robin by visit count.
   * Random — One choice from the Random pool on this node is shown per visit,
   *          selected deterministically from a hash of (graph, node, visit count).
   */
  enum class SequenceMode : uint8_t { None, Once, Cycle, Random };

  /**
   * @brief A single outgoing edge from a Choice node.
   */
  struct ChoiceEdge {
    std::string label;     ///< Displayed to the player.
    std::string target_id; ///< ID of the destination node.

    /// Optional boolean expression evaluated against the FlagStore.
    /// The edge is hidden when the expression evaluates to false.
    /// An absent or empty condition means the edge is always visible.
    std::optional<std::string> condition;

    /// Action strings executed when this edge is taken.
    /// State mutations are applied immediately; engine hook calls are returned
    /// to the platform for dispatch.
    std::vector<std::string> actions;

    /// Sequencing behaviour across repeated visits to this Choice node.
    SequenceMode sequence = SequenceMode::None;

    /// If set, this edge is only offered after the current Choice node has been
    /// visited at least N times (N >= 1).
    std::optional<int> min_visits;
  };

  /**
   * @brief One node in the dialogue graph.
   */
  struct Node {
    std::string id; ///< Unique within the graph.
    NodeType type = NodeType::End;

    std::string text;    ///< Body text (Talk nodes only).
    std::string next_id; ///< Target node id (Talk and Event nodes).

    std::vector<ChoiceEdge> choices; ///< Choice options (Choice nodes only).

    /// Action strings executed when this Event node is processed.
    std::vector<std::string> actions;

    /// Arbitrary key-value pairs passed through to the presentation layer.
    /// Ignored by core dialogue logic.
    // C++23: flat_map — cold, read-only after load; sorted contiguous pairs hit fewer cache lines
    std::flat_map<std::string, std::string> metadata;
  };

  /**
   * @brief Loaded and validated dialogue graph. Owns its nodes.
   *
   * id_to_index is built once at load time for O(log n) binary-search lookup
   * without pointer instability across node vector reallocation.
   */
  struct Graph {
    std::string graph_id;
    std::string speaker;
    std::vector<Node> nodes;

    // C++23: flat_map — built once, then read-only; sorted contiguous keys hit
    // fewer cache lines per lookup than a hash bucket chain
    std::flat_map<std::string, std::size_t> id_to_index;

    /// Default variable values loaded from the JSON "variables" object.
    /// Copied into the FlagStore on dialogue start without overwriting
    /// already-set values from prior sessions.
    // C++23: flat_map — small set of named ints; sorted iteration is predictable
    std::flat_map<std::string, int> variables;

    /**
     * @brief Find a node by id.
     * @param id Node identifier to look up.
     * @return Pointer to the node, or nullptr if not found.
     */
    [[nodiscard]] const Node *find(const std::string &id) const noexcept {
      auto it = id_to_index.find(id);
      return (it != id_to_index.end()) ? &nodes[it->second] : nullptr;
    }
  };

  /**
   * @brief Runtime traversal state for an active dialogue session.
   *
   * Lives alongside GameState in the shell, NOT inside World.
   * graph is non-owning — the Graph is stored in a Registry.
   */
  struct State {
    bool active = false;
    std::string current_id; ///< ID of the node currently being processed.
    const Graph *graph = nullptr;
    int selected_choice = 0; ///< Cursor into the VISIBLE choice list.

    /** @brief Deactivate and clear all fields. */
    void reset() noexcept {
      active = false;
      current_id.clear();
      graph = nullptr;
      selected_choice = 0;
    }
  };

} // namespace corundum::gameplay::dialogue
