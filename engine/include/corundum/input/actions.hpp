#pragma once
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace corundum::input {

  /**
   * @brief Game input actions supported by the engine.
   */
  enum class Action : uint8_t { MoveUp, MoveDown, MoveLeft, MoveRight, Select, Cancel, Quit, Count };

  /**
   * @brief Total number of input actions.
   */
  inline constexpr auto k_action_count = static_cast<std::size_t>(Action::Count);

  /**
   * @brief Container for actions pressed this frame.
   *
   * Zero-allocation result type; small enough to return by value in registers.
   * Supports range-for iteration via begin/end.
   */
  struct PressedActions {
    std::array<Action, k_action_count> actions{};
    std::size_t count{};

    /**
     * @brief Returns an iterator to the first pressed action.
     * @return Iterator to the start of the pressed actions range.
     */
    [[nodiscard]] auto begin() const noexcept {
      return actions.begin();
    }

    /**
     * @brief Returns an iterator to one past the last pressed action.
     * @return Iterator to the end of the pressed actions range.
     */
    [[nodiscard]] auto end() const noexcept {
      return actions.begin() + static_cast<std::ptrdiff_t>(count);
    }

    /**
     * @brief Returns the number of pressed actions this frame.
     * @return Count of pressed actions.
     */
    [[nodiscard]] std::size_t size() const noexcept {
      return count;
    }

    /**
     * @brief Checks if any actions were pressed this frame.
     * @return true if no actions were pressed, false otherwise.
     */
    [[nodiscard]] bool empty() const noexcept {
      return count == 0;
    }
  };

  /**
   * @brief Per-frame snapshot of digital action state.
   *
   * Updated once per event-poll cycle. Use @c held to poll sustained input
   * and @c pressed for one-frame-only triggers.
   */
  struct InputState {
    /**
     * @brief True while the key bound to an action is held down.
     */
    std::bitset<k_action_count> held{};
    /**
     * @brief True only on the frame the key bound to an action was pressed.
     */
    std::bitset<k_action_count> pressed{};

    /**
     * @brief Checks if the specified action is currently held.
     * @param action The input action to query.
     * @return true if the action is held, false otherwise.
     */
    [[nodiscard]] bool is_held(Action action) const noexcept {
      return held.test(static_cast<std::size_t>(action));
    }

    /**
     * @brief Checks if the specified action was pressed this frame.
     * @param action The input action to query.
     * @return true if the action was pressed this frame, false otherwise.
     */
    [[nodiscard]] bool is_pressed(Action action) const noexcept {
      return pressed.test(static_cast<std::size_t>(action));
    }
  };

  /**
   * @brief Resets all pressed flags to false.
   *
   * @param state Input state to clear.
   * @pre Call after consuming pressed actions to prevent re-triggering — either before the
   *      next event-poll cycle or after each fixed-step simulation iteration.
   */
  inline void clear_pressed(InputState &state) noexcept {
    state.pressed.reset();
  }

  /**
   * @brief Accumulates freshly-polled input into destination state.
   *
   * Copies @p src held state directly (always reflects current key state) and
   * ORs @p src pressed bits into @p dst so that presses are preserved across
   * render frames between fixed-step simulation ticks.
   *
   * @param[in,out] dst Destination state to accumulate into.
   * @param[in]     src Source state from the current event-poll cycle.
   */
  inline void accumulate_input(InputState &dst, const InputState &src) noexcept {
    dst.held = src.held;
    dst.pressed |= src.pressed;
  }

  /**
   * @brief Collect all actions that were pressed this frame.
   *
   * @param state Current input state to query.
   * @return Container of every Action whose pressed flag is set.
   * @note Zero-allocation; result is returned by value.
   */
  [[nodiscard]] inline PressedActions pressed_actions(const InputState &state) {
    PressedActions result{};
    for (std::size_t i = 0; i < k_action_count; ++i) {
      if (state.pressed.test(i)) {
        result.actions[result.count++] = static_cast<Action>(i);
      }
    }
    return result;
  }

} // namespace corundum::input
