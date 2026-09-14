// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::dialogue {

  const Node *advance(const Graph &graph, const Node &node, int choice_index) noexcept {
    switch (node.type) {

      case NodeType::Talk:
      case NodeType::Event:
        return graph.find(node.next_id);

      case NodeType::Choice: {
        const auto idx = static_cast<std::size_t>(choice_index);
        if (choice_index < 0 || idx >= node.choices.size())
          return nullptr;
        return graph.find(node.choices[idx].target_id);
      }

      case NodeType::End:
        return nullptr;
      default:
        std::unreachable();
    }

    return nullptr;
  }

  namespace {

    // C++23: std::ranges::fold_left — FNV-1a mix over the hashed characters.
    constexpr auto k_fnv_mix = [](std::size_t hash, char c) noexcept -> std::size_t {
      return (hash ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
    };

    /// Deterministic per-visit pick among @p total_random Random edges — hashes
    /// (graph, node, visit) so a visit always shows the same edge without storing state.
    std::size_t random_slot(std::string_view graph_id, std::string_view node_id, int visit_cnt,
                            std::size_t total_random) {
      std::size_t hash = std::ranges::fold_left(graph_id, 14695981039346656037ULL, k_fnv_mix);
      hash = std::ranges::fold_left(node_id, hash, k_fnv_mix);
      hash ^= static_cast<std::size_t>(visit_cnt) * 2654435761ULL;
      return hash % total_random;
    }

    /// Applies @p edge's sequencing mode, advancing the Cycle/Random counters, and
    /// reports whether it survives for this visit.
    bool passes_sequence(const ChoiceEdge &edge, const corundum::world::FlagStore &flags, std::string_view graph_id,
                         std::string_view node_id, std::size_t edge_index, std::size_t cycle_slot,
                         std::size_t random_slot_index, std::size_t &cycle_index, std::size_t &random_index) {
      switch (edge.sequence) {
        case SequenceMode::None:
          return true;
        case SequenceMode::Once:
          return !corundum::world::has_flag(flags, once_flag_key(graph_id, node_id, edge_index));
        case SequenceMode::Cycle:
          return cycle_index++ == cycle_slot;
        case SequenceMode::Random:
          return random_index++ == random_slot_index;
      }
      std::unreachable();
    }

  } // namespace

  std::vector<std::size_t> visible_choices(const Node &node, const corundum::world::FlagStore &flags,
                                           std::string_view graph_id, const quest::Registry *quests,
                                           std::string_view zone_id) {
    std::vector<std::size_t> result;
    if (node.type != NodeType::Choice)
      return result;

    const int visit_cnt = corundum::world::visit_count(flags, visit_flag_key(graph_id, node.id));

    std::size_t total_cycle = 0;
    std::size_t total_random = 0;
    for (const ChoiceEdge &edge : node.choices) {
      if (edge.sequence == SequenceMode::Cycle)
        ++total_cycle;
      if (edge.sequence == SequenceMode::Random)
        ++total_random;
    }

    const std::size_t cycle_slot =
        (total_cycle > 0) ? static_cast<std::size_t>(visit_cnt > 0 ? visit_cnt - 1 : 0) % total_cycle : 0;
    const std::size_t random_slot_index =
        (total_random > 0) ? random_slot(graph_id, node.id, visit_cnt, total_random) : 0;

    std::size_t cycle_index = 0;
    std::size_t random_index = 0;
    for (std::size_t i = 0; i < node.choices.size(); ++i) {
      const ChoiceEdge &edge = node.choices[i];

      if (!passes_sequence(edge, flags, graph_id, node.id, i, cycle_slot, random_slot_index, cycle_index, random_index))
        continue;

      // Compiled at load; a malformed expression is rejected when the graph loads.
      if (edge.condition.has_value() && !evaluate(*edge.condition, flags, quests, graph_id, zone_id))
        continue;

      if (edge.min_visits.has_value() && visit_cnt < *edge.min_visits)
        continue;

      result.push_back(i);
    }

    return result;
  }

} // namespace corundum::dialogue
