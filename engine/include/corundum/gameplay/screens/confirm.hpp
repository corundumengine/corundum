// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>

namespace corundum::gameplay {
  class Gameplay;
}

namespace corundum::gameplay::screens {

  /** @brief State of the shared yes/no confirmation modal (GameMode Confirm).
   *
   *  One instance serves every prompt that needs a confirm — Quit to Title and overwriting a
   *  saved slot today — so the framework carries a single ConfirmState on Gameplay rather than
   *  one per caller. Open it through Gameplay::open_confirm(), which pushes the layer.
   *
   *  A stateful class in the AGENTS.md sense: the pending callback outlives the call that set
   *  it, and Confirm is popped when the player answers or backs out. */
  struct ConfirmState {
    /** @brief Prompt drawn on the top line, e.g. "Quit to Title?". */
    std::string question{};

    /** @brief Run once when the player picks Yes; cleared on every close. */
    std::function<void(Gameplay &)> on_yes{};

    /** @brief True while Yes is highlighted; false while No is. */
    bool yes_selected{true};
  };

} // namespace corundum::gameplay::screens
