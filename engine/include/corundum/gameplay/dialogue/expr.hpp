#pragma once

#include <corundum/gameplay/flags.hpp>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::gameplay::dialogue {

  /**
   * @brief Describes a condition expression parse or evaluation failure.
   */
  struct ExprError {
    std::string message;
  };

  /**
   * @brief Evaluate a boolean condition expression against a variable store.
   *
   * Supported syntax:
   *   - Integer literals: 5, -3
   *   - Boolean literals: true, false
   *   - Identifiers: resolved via visit_count() against vars (missing = 0)
   *   - Comparison: ==, !=, <, >, <=, >=
   *   - Boolean: &&, ||, !
   *   - Parentheses: (expr)
   *
   * An empty expression always returns true (unconditionally visible).
   * Comparing a variable with true/false uses truthiness (non-zero == true).
   *
   * @param expr The condition string to evaluate.
   * @param vars Variable values resolved via visit_count().
   * @return true or false on success; ExprError describing the failure.
   */
  [[nodiscard]] std::expected<bool, ExprError> eval_condition(std::string_view expr,
                                                              const corundum::gameplay::FlagStore &vars);

} // namespace corundum::gameplay::dialogue
