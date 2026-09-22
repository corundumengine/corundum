// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/world/portals/portal.hpp>
#include <corundum/world/portals/transition_prompt.hpp>

#include <corundum/input/actions.hpp>

namespace corundum::world {

  namespace {
    /** @brief Derive the transition stashed by @ref TransitionPrompt from its portal. */
    MapTransition transition_from(const Portal &portal) {
      return {
          .target_map = portal.target_map,
          .spawn_col = portal.spawn_col,
          .spawn_row = portal.spawn_row,
          .return_to_world = portal.return_to_world,
      };
    }
  } // namespace

  TransitionPrompt::TransitionPrompt(const Portal &portal) noexcept
      : portal_{portal}, transition_{transition_from(portal)} {}

  bool TransitionPrompt::overlaps(float col0, float col1, float row0, float row1) const noexcept {
    return portal_overlaps(portal_, col0, col1, row0, row1);
  }

  bool TransitionPrompt::guards(const Portal &portal) const noexcept {
    return portal_ == portal;
  }

  TransitionPrompt::Step TransitionPrompt::step(const corundum::input::InputState &input) noexcept {
    using corundum::input::Action;

    // Left/Up → Yes, Right/Down → No. Only two options, so no wrap needed.
    if (input.is_pressed(Action::MoveLeft) || input.is_pressed(Action::MoveUp))
      confirm_selected_ = true;
    if (input.is_pressed(Action::MoveRight) || input.is_pressed(Action::MoveDown))
      confirm_selected_ = false;

    if (input.is_pressed(Action::Select)) {
      if (confirm_selected_)
        return Step::Confirmed;
      declined_ = true;
      return Step::Dismissed;
    }
    if (input.is_pressed(Action::Cancel)) {
      declined_ = true;
      return Step::Dismissed;
    }
    return Step::Pending;
  }

} // namespace corundum::world
