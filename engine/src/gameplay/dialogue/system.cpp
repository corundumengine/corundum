#include "corundum/input/actions.hpp"
#include <corundum/gameplay/dialogue/query.hpp>
#include <corundum/gameplay/dialogue/system.hpp>

#include <algorithm>
#include <utility>

namespace corundum::gameplay::dialogue {

  // ── Helpers ───────────────────────────────────────────────────────────────────

  static int wrap(int current, int delta, int count) {
    return (current + delta + count) % count;
  }

  // Advance state to a node, resetting the choice cursor and recording a visit.
  // Closes the dialogue if next is null or an End node.
  static void go_to(State &state, const Node *next, corundum::gameplay::FlagStore &flags) {
    if (!next || next->type == NodeType::End) {
      state.reset();
      return;
    }
    state.current_id = next->id;
    state.selected_choice = 0;
    corundum::gameplay::set_flag(flags, visit_flag_key(state.graph->graph_id, next->id));
  }

  // Flush any Event nodes we just landed on, collecting their emitted actions.
  // Returns immediately once the current node is not an Event.
  static void flush_events(State &state, corundum::gameplay::FlagStore &flags, std::vector<EventAction> &pending) {
    while (state.active && state.graph) {
      const Node *cur = state.graph->find(state.current_id);
      if (!cur || cur->type != NodeType::Event)
        break;
      pending.append_range(execute_actions(cur->actions, flags));
      go_to(state, advance(*state.graph, *cur), flags);
    }
  }

  // ── Public API ────────────────────────────────────────────────────────────────

  void start(State &state, const Graph &graph, corundum::gameplay::FlagStore &flags) {
    if (graph.nodes.empty())
      return;

    // Apply graph-level variable defaults without overwriting existing values.
    for (const auto &[k, v] : graph.variables)
      flags.try_emplace(k, v);

    state.graph = &graph;
    state.current_id = graph.nodes[0].id;
    state.selected_choice = 0;
    state.active = true;

    corundum::gameplay::set_flag(flags, visit_flag_key(graph.graph_id, graph.nodes[0].id));
  }

  std::vector<EventAction> system(State &state, const input::PressedActions &actions,
                                  corundum::gameplay::FlagStore &flags, const quest::Registry *quests) {
    std::vector<EventAction> pending;

    if (!state.active || !state.graph)
      return pending;

    const Node *node = state.graph->find(state.current_id);
    if (!node) {
      state.reset();
      return pending;
    }

    // C++23: std::ranges::contains — replaces the manual has() helper
    const auto contains = [&](corundum::input::Action a) { return std::ranges::contains(actions, a); };

    const bool select = contains(corundum::input::Action::Select);
    const bool cancel = contains(corundum::input::Action::Cancel);
    const bool up = contains(corundum::input::Action::MoveUp);
    const bool down = contains(corundum::input::Action::MoveDown);

    switch (node->type) {

    case NodeType::Talk:
      if (cancel) {
        state.reset();
        break;
      }
      if (select)
        go_to(state, advance(*state.graph, *node), flags);
      break;

    case NodeType::Choice: {
      const auto visible = visible_choices(*node, flags, state.graph->graph_id, quests);
      const int count = static_cast<int>(visible.size());

      if (count == 0) {
        state.reset();
        break;
      }

      if (state.selected_choice >= count)
        state.selected_choice = count - 1;

      if (up)
        state.selected_choice = wrap(state.selected_choice, -1, count);
      if (down)
        state.selected_choice = wrap(state.selected_choice, +1, count);

      if (select) {
        const std::size_t full_idx = visible[static_cast<std::size_t>(state.selected_choice)];
        const auto &edge = node->choices[full_idx];

        // Record the once-flag before advancing so visible_choices sees it
        // immediately on any same-frame re-check of this node.
        if (edge.sequence == SequenceMode::Once)
          corundum::gameplay::set_flag(flags, once_flag_key(state.graph->graph_id, node->id, full_idx));

        pending.append_range(execute_actions(edge.actions, flags));
        go_to(state, advance(*state.graph, *node, static_cast<int>(full_idx)), flags);
      }
      if (cancel)
        state.reset();
      break;
    }

    case NodeType::Event: {
      pending.append_range(execute_actions(node->actions, flags));
      go_to(state, advance(*state.graph, *node), flags);
      break;
    }

    case NodeType::End:
      state.reset();
      break;
    default:
      std::unreachable();
    }

    // Auto-advance through any chain of Event nodes reached after a transition.
    flush_events(state, flags, pending);

    return pending;
  }

} // namespace corundum::gameplay::dialogue
