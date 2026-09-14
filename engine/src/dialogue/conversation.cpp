// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::dialogue {

  // ── Helpers ───────────────────────────────────────────────────────────────────

  namespace {

    int wrap(int current, int delta, int count) {
      return (current + delta + count) % count;
    }

    bool pressed(const corundum::input::PressedActions &actions, corundum::input::Action action) {
      return std::ranges::contains(actions, action);
    }

  } // namespace

  // ── Conversation ──────────────────────────────────────────────────────────────

  Conversation::Conversation(const Graph &graph, corundum::world::FlagStore &flags, const quest::Registry *quests,
                             const Registry *graphs, std::string zone_id)
      : flags_(&flags), graphs_(graphs), quests_(quests), zone_id_(std::move(zone_id)) {
    start_graph(graph);
    if (!graph.nodes.empty())
      go_to(graph.nodes.data());
  }

  // ── Conversation queries ──────────────────────────────────────────────────────

  bool Conversation::is_active() const noexcept {
    return active_;
  }

  NodeType Conversation::node_type() const noexcept {
    const Node *node = current_node();
    return (node != nullptr) ? node->type : NodeType::End;
  }

  std::string_view Conversation::graph_id() const noexcept {
    return (graph_ != nullptr) ? std::string_view{graph_->graph_id} : std::string_view{};
  }

  std::string_view Conversation::current_node_id() const noexcept {
    return current_id_;
  }

  std::string_view Conversation::speaker() const noexcept {
    return (graph_ != nullptr) ? std::string_view{graph_->speaker} : std::string_view{};
  }

  std::string_view Conversation::current_text() const noexcept {
    const Node *node = current_node();
    return (node != nullptr) ? std::string_view{node->text} : std::string_view{};
  }

  std::string_view Conversation::choice_label(std::size_t full_index) const noexcept {
    const Node *node = current_node();
    if ((node == nullptr) || full_index >= node->choices.size())
      return {};
    return node->choices[full_index].label;
  }

  std::vector<std::size_t> Conversation::visible_choice_indices() const {
    const Node *node = current_node();
    if ((node == nullptr) || node->type != NodeType::Choice || (graph_ == nullptr))
      return {};
    return visible_choices(*node, *flags_, graph_->graph_id, quests_, zone_id_);
  }

  int Conversation::selected_choice() const noexcept {
    return selected_choice_;
  }

  std::size_t Conversation::call_stack_depth() const noexcept {
    return call_stack_.size();
  }

  std::string_view Conversation::resume_graph_id() const noexcept {
    if (call_stack_.empty())
      return {};
    const Graph *resume_graph = call_stack_.back().first;
    return (resume_graph != nullptr) ? std::string_view{resume_graph->graph_id} : std::string_view{};
  }

  std::string_view Conversation::resume_node_id() const noexcept {
    if (call_stack_.empty())
      return {};
    return call_stack_.back().second;
  }

  // ── Conversation internals ────────────────────────────────────────────────────

  const Node *Conversation::current_node() const noexcept {
    if (!active_ || (graph_ == nullptr))
      return nullptr;
    return graph_->find(current_id_);
  }

  void Conversation::start_graph(const Graph &graph) {
    if (graph.nodes.empty())
      return;

    // Apply graph-level variable defaults without overwriting existing values.
    for (const auto &[k, v] : graph.variables)
      flags_->try_emplace(k, v);

    graph_ = &graph;
    active_ = true;
  }

  void Conversation::go_to(const Node *next) {
    if ((next == nullptr) || next->type == NodeType::End) {
      reset();
      return;
    }
    current_id_ = next->id;
    selected_choice_ = 0;
    corundum::world::set_flag(*flags_, visit_flag_key(graph_->graph_id, next->id));
  }

  void Conversation::reset() noexcept {
    active_ = false;
    call_stack_.clear();
    current_id_.clear();
    selected_choice_ = 0;
  }

  bool Conversation::divert(const Node *current_node, int choice_index, std::vector<EventAction> &events) {
    bool diverted = false;
    std::erase_if(events, [&](const EventAction &ev) {
      if (ev.name == "goto_graph") {
        diverted = true;
        if (graphs_ == nullptr || ev.args.size() < 2) {
          std::println(stderr, "[dialogue] goto_graph needs 'graph_id' and 'node_id'");
          return true;
        }
        const Graph *target_graph = graphs_->find(ev.args[0]);
        if (target_graph == nullptr) {
          std::println(stderr, "[dialogue] goto_graph references unknown graph '{}'", ev.args[0]);
          return true;
        }
        const Node *target_node = target_graph->find(ev.args[1]);
        if (target_node == nullptr || target_node->type == NodeType::End) {
          std::println(stderr, "[dialogue] goto_graph references unknown node '{}' in '{}'", ev.args[1], ev.args[0]);
          return true;
        }

        const Node *resume = advance(*graph_, *current_node, choice_index);
        call_stack_.emplace_back(graph_, resume ? resume->id : std::string{});
        start_graph(*target_graph);
        go_to(target_node);
        return true;
      }
      if (ev.name == "return_graph") {
        diverted = true;
        if (call_stack_.empty()) {
          reset();
          return true;
        }
        const auto [resume_graph, resume_id] = call_stack_.back();
        call_stack_.pop_back();
        graph_ = resume_graph;
        go_to(resume_graph->find(resume_id));
        return true;
      }
      return false;
    });
    return diverted;
  }

  void Conversation::flush_events(std::vector<EventAction> &pending) {
    constexpr int k_max_event_hops = 1000;
    int hops = 0;
    while (active_ && (graph_ != nullptr)) {
      if (++hops > k_max_event_hops) {
        std::println(stderr, "[dialogue] event chain exceeds {} hops in '{}' — cycle detected, aborting",
                     k_max_event_hops, graph_->graph_id);
        reset();
        return;
      }
      const Node *cur = graph_->find(current_id_);
      if ((cur == nullptr) || cur->type != NodeType::Event)
        break;
      auto events = execute_actions(cur->actions, *flags_, zone_id_);
      const bool diverted = divert(cur, /*choice_index=*/0, events);
      pending.append_range(events);
      if (diverted)
        return; // jumped to another graph (or ended); stop flushing this graph
      go_to(advance(*graph_, *cur));
    }
  }

  void Conversation::handle_talk(const Node &node, const input::PressedActions &actions) {
    if (pressed(actions, corundum::input::Action::Cancel)) {
      reset();
      return;
    }

    if (!node.once) {
      if (pressed(actions, corundum::input::Action::Select))
        go_to(advance(*graph_, node));
      return;
    }

    const std::string once_key = node_once_flag_key(graph_->graph_id, node.id);
    if (corundum::world::has_flag(*flags_, once_key)) {
      // Already shown — skip straight to next_id without waiting for input.
      go_to(advance(*graph_, node));
      return;
    }

    if (pressed(actions, corundum::input::Action::Select)) {
      corundum::world::set_flag(*flags_, once_key);
      go_to(advance(*graph_, node));
    }
  }

  void Conversation::handle_choice(const Node &node, const input::PressedActions &actions,
                                   std::vector<EventAction> &pending) {
    const std::vector<std::size_t> visible = visible_choices(node, *flags_, graph_->graph_id, quests_, zone_id_);
    const int count = static_cast<int>(visible.size());

    if (count == 0) {
      reset();
      return;
    }

    if (selected_choice_ >= count)
      selected_choice_ = count - 1;

    if (pressed(actions, corundum::input::Action::MoveUp))
      selected_choice_ = wrap(selected_choice_, -1, count);
    if (pressed(actions, corundum::input::Action::MoveDown))
      selected_choice_ = wrap(selected_choice_, +1, count);

    if (pressed(actions, corundum::input::Action::Select)) {
      const std::size_t full_index = visible[static_cast<std::size_t>(selected_choice_)];
      const ChoiceEdge &edge = node.choices[full_index];

      // Record the once-flag before advancing so visible_choices sees it
      // immediately on any same-frame re-check of this node.
      if (edge.sequence == SequenceMode::Once)
        corundum::world::set_flag(*flags_, once_flag_key(graph_->graph_id, node.id, full_index));

      auto events = execute_actions(edge.actions, *flags_, zone_id_);
      const bool diverted = divert(&node, static_cast<int>(full_index), events);
      pending.append_range(events);
      if (diverted)
        return; // diverted to another graph (or ended); skip the local go_to

      go_to(advance(*graph_, node, static_cast<int>(full_index)));
    }

    if (pressed(actions, corundum::input::Action::Cancel))
      reset();
  }

  void Conversation::handle_event(const Node &node, std::vector<EventAction> &pending) {
    auto events = execute_actions(node.actions, *flags_, zone_id_);
    const bool diverted = divert(&node, /*choice_index=*/0, events);
    pending.append_range(events);
    if (diverted)
      return; // diverted to another graph (or ended); skip the local go_to

    go_to(advance(*graph_, node));
  }

  // ── Conversation public API ───────────────────────────────────────────────────

  std::vector<EventAction> Conversation::update(const input::PressedActions &actions) {
    std::vector<EventAction> pending;

    if (!active_ || (graph_ == nullptr))
      return pending;

    const Node *node = graph_->find(current_id_);
    if (node == nullptr) {
      reset();
      return pending;
    }

    switch (node->type) {
      case NodeType::Talk:
        handle_talk(*node, actions);
        break;
      case NodeType::Choice:
        handle_choice(*node, actions, pending);
        break;
      case NodeType::Event:
        handle_event(*node, pending);
        break;
      case NodeType::End:
        reset();
        break;
      default:
        std::unreachable();
    }

    // Auto-advance through any chain of Event nodes reached after a transition.
    flush_events(pending);

    return pending;
  }

} // namespace corundum::dialogue
