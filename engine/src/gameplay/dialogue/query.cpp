#include <corundum/gameplay/dialogue/expr.hpp>
#include <corundum/gameplay/dialogue/query.hpp>

#include <algorithm>
#include <utility>

namespace corundum::gameplay::dialogue {

  const Node *find_node(const Graph &graph, const std::string &id) noexcept {
    return graph.find(id);
  }

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

  std::vector<std::size_t> visible_choices(const Node &node, const corundum::gameplay::FlagStore &flags,
                                           std::string_view graph_id, const quest::Registry *quests) {
    std::vector<std::size_t> result;
    if (node.type != NodeType::Choice)
      return result;

    const int visit_cnt = corundum::gameplay::visit_count(flags, visit_flag_key(graph_id, node.id));

    // Count sequenced choices for Cycle and Random logic.
    std::size_t total_cycle = 0;
    std::size_t total_random = 0;
    for (const auto &e : node.choices) {
      if (e.sequence == SequenceMode::Cycle)
        ++total_cycle;
      if (e.sequence == SequenceMode::Random)
        ++total_random;
    }

    // Which Cycle slot is active for this visit (0-indexed among Cycle choices).
    const std::size_t cycle_slot =
        (total_cycle > 0) ? static_cast<std::size_t>(visit_cnt > 0 ? visit_cnt - 1 : 0) % total_cycle : 0;

    // Which Random choice is selected for this visit — deterministic hash so
    // the same visit always shows the same choice without storing extra state.
    std::size_t selected_random = 0;
    if (total_random > 0) {
      // C++23: std::ranges::fold_left — FNV-1a hash over graph_id and node.id characters
      constexpr auto fnv_mix = [](std::size_t h, char c) noexcept -> std::size_t {
        return (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
      };
      std::size_t h = std::ranges::fold_left(graph_id, 14695981039346656037ULL, fnv_mix);
      h = std::ranges::fold_left(node.id, h, fnv_mix);
      h ^= static_cast<std::size_t>(visit_cnt) * 2654435761ULL;
      selected_random = h % total_random;
    }

    std::size_t cycle_idx = 0;
    std::size_t random_idx = 0;

    for (std::size_t i = 0; i < node.choices.size(); ++i) {
      const auto &edge = node.choices[i];

      // ── Sequence mode ────────────────────────────────────────────────────────
      switch (edge.sequence) {
      case SequenceMode::None:
        break;

      case SequenceMode::Once:
        if (corundum::gameplay::has_flag(flags, once_flag_key(graph_id, node.id, i)))
          continue;
        break;

      case SequenceMode::Cycle:
        if (cycle_idx++ != cycle_slot)
          continue;
        break;

      case SequenceMode::Random:
        if (random_idx++ != selected_random)
          continue;
        break;
      default:
        std::unreachable();
      }

      // ── Condition expression ─────────────────────────────────────────────────
      if (!edge.condition
               .transform([&](const std::string &cond) -> bool {
                 if (cond.empty())
                   return true;
                 auto r = eval_condition(cond, flags, quests);
                 if (!r.has_value()) {
                   std::println(stderr, "[dialogue] condition eval failed '{}': {}", cond, r.error().message);
                   return false;
                 }
                 return *r;
               })
               .value_or(true))
        continue;

      // ── Min visits ───────────────────────────────────────────────────────────
      if (edge.min_visits.has_value() && visit_cnt < *edge.min_visits)
        continue;

      result.push_back(i);
    }

    return result;
  }

  bool is_terminal(const Node &node) noexcept {
    return node.type == NodeType::End;
  }

} // namespace corundum::gameplay::dialogue
