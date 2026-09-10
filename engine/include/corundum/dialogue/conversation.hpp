#pragma once

#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/flags.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::quest {
  class Registry;
}

namespace corundum::dialogue {

  class Registry;

  /**
   * @brief One active dialogue conversation.
   *
   * Constructed when a conversation starts (bind the graph and the collaborators it
   * needs — FlagStore, quest/dialogue registries, zone), stepped once per fixed step
   * with update(), and destroyed when the conversation ends. Owns its traversal state;
   * the presentation layer observes it through the read-only query methods rather than
   * reaching into a state struct.
   *
   * Conversation holds non-owning pointers to @p flags, @p quests and @p graphs — none may
   * outlive the Conversation. zone_id is copied so the Conversation is independent of the caller's string.
   */
  class Conversation {
  public:
    /**
     * @brief Start a conversation on @p graph.
     *
     * Copies graph-level variable defaults into flags without overwriting values already
     * set from a prior conversation, and records the first node's visit.
     *
     * @param graph   The dialogue graph to run. Must outlive the Conversation.
     * @param flags   FlagStore for condition evaluation and state mutations.
     * @param quests  Quest registry for quest-helper conditions; may be nullptr.
     * @param graphs  Dialogue registry resolving goto_graph targets; may be nullptr.
     * @param zone_id Current zone for `local.<key>` state; empty disables scoping.
     */
    Conversation(const Graph &graph, corundum::world::FlagStore &flags, const quest::Registry *quests = nullptr,
                 const Registry *graphs = nullptr, std::string zone_id = {});

    /**
     * @brief Advance the conversation by this frame's input actions.
     *
     * Has no effect if the Conversation is not active.
     *
     * Behaviour per node type:
     *
     *   Talk   — Select advances to next_id. Cancel closes the dialogue.
     *   Choice — MoveUp/Down moves the cursor within VISIBLE choices (wrapping).
     *            Select advances to the chosen target and executes the edge's actions.
     *            Cancel closes the dialogue. If no choices are visible the dialogue
     *            closes immediately.
     *   Event  — Actions execute automatically, no input required. Transitions to
     *            next_id immediately. Chains of Event nodes are flushed in a single call.
     *   End    — Select or Cancel closes the dialogue.
     *
     * Two EventActions are intercepted here instead of reaching the engine queue:
     * `goto_graph(graph_id, node_id)` pushes the current (graph, resume node) onto the
     * call stack and jumps to @p node_id in @p graph_id; `return_graph()` pops the stack
     * and resumes there (or ends the dialogue when the stack is empty). Neither requires
     * `graphs` to be non-null — a `goto_graph` without the registry warns and is dropped.
     *
     * @param actions Input actions for this frame.
     * @return All EventActions emitted this step, for the platform to dispatch.
     */
    [[nodiscard]] std::vector<EventAction> update(const input::PressedActions &actions);

    // ── Read-only queries for the presentation layer ──

    /// True while a conversation is running.
    [[nodiscard]] bool is_active() const noexcept;

    /// Type of the node currently being presented. End when the Conversation is inactive
    /// or the current node cannot be resolved.
    [[nodiscard]] NodeType node_type() const noexcept;

    /// Graph id of the running conversation; empty when inactive.
    [[nodiscard]] std::string_view graph_id() const noexcept;

    /// Id of the node currently being processed; empty when inactive.
    [[nodiscard]] std::string_view current_node_id() const noexcept;

    /// Speaker display name of the running graph; empty when inactive.
    [[nodiscard]] std::string_view speaker() const noexcept;

    /// Body text of the current node (Talk nodes); empty otherwise.
    [[nodiscard]] std::string_view current_text() const noexcept;

    /// Display label of choice @p full_index (index into the node's full choice list);
    /// empty when out of range or the current node is not a Choice.
    [[nodiscard]] std::string_view choice_label(std::size_t full_index) const noexcept;

    /// Indices (into the node's full choice list) of the choices visible this step.
    /// Empty for non-Choice nodes.
    [[nodiscard]] std::vector<std::size_t> visible_choice_indices() const;

    /// Cursor into the visible choice list (for the presentation layer to highlight).
    [[nodiscard]] int selected_choice() const noexcept;

    /// Number of pending goto_graph return frames.
    [[nodiscard]] std::size_t call_stack_depth() const noexcept;

    /// Graph id the conversation would resume in on the next return_graph();
    /// empty when the stack is empty.
    [[nodiscard]] std::string_view resume_graph_id() const noexcept;

    /// Node id the conversation would resume at on the next return_graph();
    /// empty when the stack is empty.
    [[nodiscard]] std::string_view resume_node_id() const noexcept;

  private:
    /// Node currently being processed; nullptr when inactive or unresolvable.
    [[nodiscard]] const Node *current_node() const noexcept;

    /// Begin running @p graph: apply variable defaults and jump to its first node.
    /// No-op when the graph has no nodes.
    void start_graph(const Graph &graph);

    /// Advance to a node, resetting the choice cursor and recording a visit. Ends the
    /// conversation if next is null or an End node.
    void go_to(const Node *next);

    /// Handle goto_graph / return_graph EventActions before they reach the engine queue.
    /// Each handled divert is erased from `events`. Returns true when a divert consumed
    /// the flow — the caller must not advance the current graph or flush its event chain.
    bool divert(const Node *current_node, int choice_index, std::vector<EventAction> &events);

    /// Flush any Event nodes just landed on, collecting their emitted actions. Returns
    /// once the current node is not an Event.
    void flush_events(std::vector<EventAction> &pending);

    /// Deactivate and clear all traversal state.
    void reset() noexcept;

    bool active_ = false;
    std::vector<std::pair<const Graph *, std::string>> call_stack_;
    std::string current_id_;
    corundum::world::FlagStore *flags_;
    const Graph *graph_ = nullptr;
    const Registry *graphs_ = nullptr;
    const quest::Registry *quests_ = nullptr;
    int selected_choice_ = 0;
    std::string zone_id_;
  };

} // namespace corundum::dialogue
