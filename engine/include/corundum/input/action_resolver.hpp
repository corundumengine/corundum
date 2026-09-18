// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>

#include <array>
#include <bitset>
#include <cstddef>

namespace corundum::input {

  /** @brief Upper bound on the number of physical input sources one resolver tracks. */
  inline constexpr std::size_t k_max_input_sources = 64;

  /** @brief Folds many physical input sources bound to one Action into a single held bit and press edge.
   *
   *  Several bindings routinely map to the same Action (W and Up both mean MoveUp;
   *  keyboard Enter, mouse-left and gamepad A all mean Select). Writing
   *  InputState::held directly from each source lets one source's release clear an
   *  action another source still holds. This type records which sources are active
   *  and derives the action's bits from that set instead.
   *
   *  @note Sources are opaque indices assigned by the caller. They must be unique
   *        within one resolver but need not be contiguous.
   *  @note Not thread-safe; drive it from the main thread only.
   */
  class ActionResolver {
  public:
    /** @brief Record a source transition and fold it into @p state.
     *
     *  A press edge is raised only when @p action moves from "no source active" to
     *  "at least one source active", so pressing a second binding while the first is
     *  still held is not a new press.
     *
     *  @param[in]     source  Stable index of the source, in [0, k_max_input_sources).
     *  @param[in]     action  Action the source is bound to.
     *  @param[in]     down    New state of the source.
     *  @param[in,out] state   Per-poll input state updated in place.
     *  @pre @p source < k_max_input_sources.
     */
    void update(std::size_t source, Action action, bool down, InputState &state) noexcept {
      auto &sources = active_[static_cast<std::size_t>(action)];
      const bool was_active = sources.any();
      sources[source] = down; // operator[], not set(), so the noexcept contract holds
      const bool active = sources.any();

      if (active && !was_active)
        state.pressed[static_cast<std::size_t>(action)] = true;
      state.held[static_cast<std::size_t>(action)] = active;
    }

  private:
    std::array<std::bitset<k_max_input_sources>, k_action_count> active_{};
  };

} // namespace corundum::input
