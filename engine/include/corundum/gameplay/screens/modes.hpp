// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/ui_stack.hpp>

namespace corundum::gameplay::screens {

  /** @brief Gameplay screens, defined from world::k_first_extension_mode so the engine
   *  runtime never has to name them.
   *
   *  Named like enum constants (not engine constexpr constants) because that is what they
   *  stand in for: each is a GameMode value.
   *
   *  @note Each must push its layer when it opens; see the UIStack invariant. */
  // NOLINTBEGIN(readability-identifier-naming): these are GameMode values, not engine constants.
  inline constexpr world::GameMode Dialogue{static_cast<world::GameMode>(world::k_first_extension_mode + 0)};

  inline constexpr world::GameMode Inventory{static_cast<world::GameMode>(world::k_first_extension_mode + 1)};

  inline constexpr world::GameMode Journal{static_cast<world::GameMode>(world::k_first_extension_mode + 2)};

  inline constexpr world::GameMode Codex{static_cast<world::GameMode>(world::k_first_extension_mode + 3)};

  inline constexpr world::GameMode Map{static_cast<world::GameMode>(world::k_first_extension_mode + 4)};

  inline constexpr world::GameMode Loot{static_cast<world::GameMode>(world::k_first_extension_mode + 5)};

  inline constexpr world::GameMode Barter{static_cast<world::GameMode>(world::k_first_extension_mode + 6)};

  /** @brief Framing title screen, pushed over the scene the engine loaded at startup. */
  inline constexpr world::GameMode Title{static_cast<world::GameMode>(world::k_first_extension_mode + 7)};

  /** @brief Save/Load slot browser, opened in save or load mode. */
  inline constexpr world::GameMode SaveLoad{static_cast<world::GameMode>(world::k_first_extension_mode + 8)};

  /** @brief Game-over screen, opened by the game (never the framework). */
  inline constexpr world::GameMode GameOver{static_cast<world::GameMode>(world::k_first_extension_mode + 9)};

  /** @brief Scrolling credits, opened from the Title when a credits file is configured. */
  inline constexpr world::GameMode Credits{static_cast<world::GameMode>(world::k_first_extension_mode + 10)};

  /** @brief Two-phase loading overlay; see Gameplay::begin_load() (implemented with the screen). */
  inline constexpr world::GameMode Loading{static_cast<world::GameMode>(world::k_first_extension_mode + 11)};

  /** @brief Modal yes/no confirmation, carrying no state of its own beyond Gameplay::confirm. */
  inline constexpr world::GameMode Confirm{static_cast<world::GameMode>(world::k_first_extension_mode + 12)};
  // NOLINTEND(readability-identifier-naming)

  /** @brief True when @p mode is one of the four screens reachable from the menu hub.
   *
   *  The hub has no GameMode of its own: it is the convention that `UIStack::top()` is one of
   *  these four, which a single gamepad Hub button (or the I/J/C/M keyboard hotkeys) opens and
   *  switches between.
   */
  [[nodiscard]] constexpr bool is_hub_mode(world::GameMode mode) noexcept {
    return mode == Inventory || mode == Journal || mode == Codex || mode == Map;
  }

} // namespace corundum::gameplay::screens
