#pragma once

#include <corundum/gameplay/flags.hpp>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace corundum::gameplay::dialogue {

  /**
   * @brief A variable mutation applied to the FlagStore.
   *
   * Parsed from: "var = value", "var += value", "var -= value".
   * value may be an integer, true (1), or false (0).
   */
  struct StateAction {
    std::string var;
    enum class Op : uint8_t { Assign, Add, Sub } op = Op::Assign;
    int value = 0;
  };

  /**
   * @brief An engine hook call emitted for the platform to dispatch.
   *
   * Parsed from: "name(args...)" where args are quoted strings, identifiers, or integers.
   */
  struct EventAction {
    std::string name;              ///< Function name, e.g. "play_sound"
    std::vector<std::string> args; ///< Arguments (unquoted)
  };

  /** @brief One parsed action — either a state mutation or an engine hook. */
  using Action = std::variant<StateAction, EventAction>;

  /** @brief Describes an action parse failure. */
  struct ActionError {
    std::string message;
  };

  /**
   * @brief Parse a single action string into a typed Action.
   *
   * Recognised forms:
   *   var = value    — StateAction::Assign
   *   var += value   — StateAction::Add
   *   var -= value   — StateAction::Sub
   *   name(args...)  — EventAction
   *
   * @param src The action string to parse.
   * @return The parsed Action, or ActionError on failure.
   */
  [[nodiscard]] std::expected<Action, ActionError> parse_action(std::string_view src);

  /**
   * @brief Execute a list of action strings against a FlagStore.
   *
   * StateActions are applied immediately. EventActions are collected and returned
   * for the platform to dispatch — the dialogue system never executes them directly.
   *
   * @param actions Action strings to parse and execute.
   * @param flags   FlagStore to apply state mutations to.
   * @return All EventActions found, in order.
   */
  [[nodiscard]] std::vector<EventAction> execute_actions(std::span<const std::string> actions,
                                                         corundum::gameplay::FlagStore &flags);

} // namespace corundum::gameplay::dialogue
