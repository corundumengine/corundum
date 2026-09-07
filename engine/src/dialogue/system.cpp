#include "corundum/input/actions.hpp"
#include <corundum/dialogue/query.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/dialogue/system.hpp>

#include <algorithm>
#include <print>
#include <string_view>
#include <utility>

namespace corundum::dialogue {

  // ── Helpers ───────────────────────────────────────────────────────────────────

  static int wrap(int current, int delta, int count) {
    return (current + delta + count) % count;
  }

  // Advance state to a node, resetting the choice cursor and recording a visit.
  // Closes the dialogue if next is null or an End node.
  static void go_to(State &state, const Node *next, corundum::world::FlagStore &flags) {
    if (!next || next->type == NodeType::End) {
      state.reset();
      return;
    }
    state.current_id = next->id;
    state.selected_choice = 0;
    corundum::world::set_flag(flags, visit_flag_key(state.graph->graph_id, next->id));
  }

  // Handle goto_graph / return_graph EventActions before they reach the engine
  // queue. Each handled divert is erased from `events`. Returns true when a
  // divert consumed the flow — callers must not advance the current graph or
  // flush its event chain.
  //
  // goto_graph(graph_id, node_id):
  //   Pushes (current graph, current node's successor) onto state.call_stack so
  //   a later return_graph() lands where this graph would have gone, then starts
  //   the target graph at node_id. The resume point is the successor — an Event
  //   node that diverts must not re-fire itself on return.
  // return_graph():
  //   Pops the top (graph, node) and resumes there. An empty stack means there
  //   is nothing to return to, so the dialogue ends.
  static bool divert(State &state, const Registry *graphs, corundum::world::FlagStore &flags, const Node *current_node,
                     int choice_index, std::vector<EventAction> &events) {
    bool diverted = false;
    std::erase_if(events, [&](const EventAction &ev) {
      if (ev.name == "goto_graph") {
        diverted = true;
        if (graphs == nullptr || ev.args.size() < 2) {
          std::println(stderr, "[dialogue] goto_graph needs 'graph_id' and 'node_id'");
          return true;
        }
        const Graph *target_graph = graphs->find(ev.args[0]);
        if (target_graph == nullptr) {
          std::println(stderr, "[dialogue] goto_graph references unknown graph '{}'", ev.args[0]);
          return true;
        }
        const Node *target_node = target_graph->find(ev.args[1]);
        if (target_node == nullptr || target_node->type == NodeType::End) {
          std::println(stderr, "[dialogue] goto_graph references unknown node '{}' in '{}'", ev.args[1], ev.args[0]);
          return true;
        }

        const Node *resume = advance(*state.graph, *current_node, choice_index);
        state.call_stack.emplace_back(state.graph, resume ? resume->id : std::string{});
        start(state, *target_graph, flags);
        state.current_id = target_node->id;
        state.selected_choice = 0;
        return true;
      }
      if (ev.name == "return_graph") {
        diverted = true;
        if (state.call_stack.empty()) {
          state.reset();
          return true;
        }
        const auto [resume_graph, resume_id] = state.call_stack.back();
        state.call_stack.pop_back();
        state.graph = resume_graph;
        go_to(state, resume_graph->find(resume_id), flags);
        return true;
      }
      return false;
    });
    return diverted;
  }

  // Flush any Event nodes we just landed on, collecting their emitted actions.
  // Returns immediately once the current node is not an Event.
  static void flush_events(State &state, corundum::world::FlagStore &flags, const Registry *graphs,
                           std::string_view zone_id, std::vector<EventAction> &pending) {
    constexpr int k_max_event_hops = 1000;
    int hops = 0;
    while (state.active && state.graph) {
      if (++hops > k_max_event_hops) {
        std::println(stderr, "[dialogue] event chain exceeds {} hops in '{}' — cycle detected, aborting",
                     k_max_event_hops, state.graph->graph_id);
        state.reset();
        return;
      }
      const Node *cur = state.graph->find(state.current_id);
      if (!cur || cur->type != NodeType::Event)
        break;
      auto events = execute_actions(cur->actions, flags, zone_id);
      if (divert(state, graphs, flags, cur, /*choice_index=*/0, events))
        return; // jumped to another graph (or ended); stop flushing this graph
      pending.append_range(events);
      go_to(state, advance(*state.graph, *cur), flags);
    }
  }

  // ── Public API ────────────────────────────────────────────────────────────────

  void start(State &state, const Graph &graph, corundum::world::FlagStore &flags) {
    if (graph.nodes.empty())
      return;

    // Apply graph-level variable defaults without overwriting existing values.
    for (const auto &[k, v] : graph.variables)
      flags.try_emplace(k, v);

    state.graph = &graph;
    state.current_id = graph.nodes[0].id;
    state.selected_choice = 0;
    state.active = true;

    corundum::world::set_flag(flags, visit_flag_key(graph.graph_id, graph.nodes[0].id));
  }

  std::vector<EventAction> system(State &state, const input::PressedActions &actions, corundum::world::FlagStore &flags,
                                  const quest::Registry *quests, const Registry *graphs, std::string_view zone_id) {
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
        if (node->once) {
          const auto once_key = node_once_flag_key(state.graph->graph_id, node->id);
          if (corundum::world::has_flag(flags, once_key)) {
            // Already shown — skip straight to next_id without waiting for input.
            go_to(state, advance(*state.graph, *node), flags);
            break;
          }
          if (select) {
            corundum::world::set_flag(flags, once_key);
            go_to(state, advance(*state.graph, *node), flags);
          }
          break;
        }
        if (select)
          go_to(state, advance(*state.graph, *node), flags);
        break;

      case NodeType::Choice: {
        const auto visible = visible_choices(*node, flags, state.graph->graph_id, quests, zone_id);
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
            corundum::world::set_flag(flags, once_flag_key(state.graph->graph_id, node->id, full_idx));

          auto events = execute_actions(edge.actions, flags, zone_id);
          if (divert(state, graphs, flags, node, static_cast<int>(full_idx), events))
            break; // diverted to another graph (or ended); skip the local go_to

          pending.append_range(events);
          go_to(state, advance(*state.graph, *node, static_cast<int>(full_idx)), flags);
        }
        if (cancel)
          state.reset();
        break;
      }

      case NodeType::Event: {
        auto events = execute_actions(node->actions, flags, zone_id);
        if (divert(state, graphs, flags, node, /*choice_index=*/0, events))
          break; // diverted to another graph (or ended); skip the local go_to

        pending.append_range(events);
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
    flush_events(state, flags, graphs, zone_id, pending);

    return pending;
  }

} // namespace corundum::dialogue