// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace corundum::world {

  /** @brief First GameMode value reserved for extensions.
   *
   *  The engine runtime owns the enumerators below; the gameplay framework defines its own
   *  modes (Dialogue, Inventory, Journal, ...) as constexpr GameMode values from this base,
   *  so UIStack::top() and Scene::mode() may return a value no engine switch names.
   */
  inline constexpr std::uint8_t k_first_extension_mode = 16;

  /** @brief Which screen owns the current fixed step.
   *
   *  Exploring is the base state — it is represented by an empty UIStack, not a pushed
   *  layer — so top() returns it whenever no screen is open. Values from
   *  k_first_extension_mode upward are extension modes, not engine enumerators.
   *
   *  Invariant: every open modal pushes a UIStack layer. Extension modes must push a layer
   *  too — the HUD-layer gate and UIStack-based step gating trust that an open modal means a
   *  non-empty stack, so a mode that does not push silently re-enables the HUD under itself.
   */
  enum class GameMode : std::uint8_t {
    Exploring, ///< Base state; represented by an empty UIStack, never a pushed layer.
    Prompt,    ///< Portal-confirm prompt.
    Menu,      ///< Pause menu.
    Settings,  ///< In-game settings screen.
  };

  /** @brief The ordered stack of open UI screens, top last.
   *
   *  A value type: public state, per-call methods, no stored collaborators. Opening a screen
   *  pushes it; Cancel/close pops one layer, so a journal opened over a dialogue returns to
   *  the dialogue rather than resetting to Exploring. The common case is a one-element (or
   *  empty) stack, so this stays a flat array, not a heap container — UI depth is tiny and
   *  bounded.
   */
  class UIStack {
  public:
    /** @brief Upper bound on stacked screens; push() is a no-op past it. */
    static constexpr std::size_t k_max_depth = 8;

    /** @brief The screen on top of the stack, or GameMode::Exploring when it is empty. */
    [[nodiscard]] GameMode top() const noexcept {
      return count_ == 0 ? GameMode::Exploring : layers_[count_ - 1];
    }

    /** @brief True when @p mode is anywhere in the stack, not just on top. */
    [[nodiscard]] bool contains(GameMode mode) const noexcept {
      for (std::size_t i = 0; i < count_; ++i) {
        if (layers_[i] == mode)
          return true;
      }
      return false;
    }

    /** @brief True when no screen is open (the stack is in the Exploring base state). */
    [[nodiscard]] bool empty() const noexcept {
      return count_ == 0;
    }

    /** @brief Number of open screens. */
    [[nodiscard]] std::size_t size() const noexcept {
      return count_;
    }

    /** @brief Push @p mode as the new top screen.
     *  @note Ignored for GameMode::Exploring (the empty-stack base) and when already at k_max_depth.
     */
    void push(GameMode mode) noexcept {
      if (mode == GameMode::Exploring || count_ == k_max_depth)
        return;
      layers_[count_++] = mode;
    }

    /** @brief Remove the top screen; no-op when the stack is empty. */
    void pop() noexcept {
      if (count_ > 0)
        --count_;
    }

    /** @brief Remove every screen, returning to the Exploring base. */
    void clear() noexcept {
      count_ = 0;
    }

  private:
    std::array<GameMode, k_max_depth> layers_{};

    std::size_t count_{};
  };

} // namespace corundum::world
