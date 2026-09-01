#pragma once

#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/flags.hpp>

#include <vector>

namespace corundum::quest {
  class Registry;
}

namespace corundum::dialogue {

  /**
   * @brief Drives a State forward given the frame's input actions.
   *
   * Call once per frame while state.active is true.
   * Has no effect if state.active is false or state.graph is nullptr.
   *
   * Behaviour per node type:
   *
   *   Talk   — Select advances to next_id. Cancel closes the dialogue.
   *   Choice — MoveUp/Down moves the cursor within VISIBLE choices (wrapping).
   *            Select advances to the chosen target and executes the edge's
   *            actions. Cancel closes the dialogue.
   *            If no choices are visible the dialogue closes immediately.
   *   Event  — Actions execute automatically, no input required. Transitions
   *            to next_id immediately. Chains of Event nodes are flushed in a
   *            single call.
   *   End    — Select or Cancel closes the dialogue.
   *
   * @param state   Active dialogue traversal state.
   * @param actions Input actions for this frame.
   * @param flags   FlagStore for condition evaluation and state mutations.
   * @return        All EventActions emitted this frame, for the platform to dispatch.
   */
  [[nodiscard]] std::vector<EventAction> system(State &state, const input::PressedActions &actions,
                                                corundum::world::FlagStore &flags,
                                                const quest::Registry *quests = nullptr);

  /**
   * @brief Opens a dialogue at the graph's first node.
   *
   * Copies graph-level variable defaults into flags without overwriting
   * values already set from a prior session.
   * Prefer this over mutating State directly.
   *
   * @param state Active dialogue state to initialise.
   * @param graph The dialogue graph to run.
   * @param flags FlagStore to record the first visit and apply defaults.
   */
  void start(State &state, const Graph &graph, corundum::world::FlagStore &flags);

} // namespace corundum::dialogue
