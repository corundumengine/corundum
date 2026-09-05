#pragma once

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/world/flags.hpp>
#include <expected>

namespace corundum::quest {
  class Registry;
}

namespace corundum::dialogue {

  /**
   * @brief Evaluate a boolean condition expression string against a variable store.
   *
   * Thin shim over compile() → evaluate() kept for callers that hold condition
   * strings (e.g. tests). Content loads compile once into a CompiledExpr and
   * evaluate that instead — see compiled_expr.hpp. An empty expression always
   * returns true; a malformed one yields an ExprError (never a silent false).
   *
   * Supported syntax is unchanged from before the split: integers, booleans,
   * bare flag keys, comparisons, &&/||/!, parentheses, and the quest / item /
   * reputation helpers.
   *
   * @param expr The condition string to evaluate.
   * @param vars Variable values resolved via visit_count().
   * @param zone_id Current zone; `local.<key>` identifiers resolve against it.
   * @return true or false on success; ExprError describing the failure.
   */
  [[nodiscard]] std::expected<bool, ExprError> eval_condition(std::string_view expr,
                                                              const corundum::world::FlagStore &vars,
                                                              const quest::Registry *quests = nullptr,
                                                              std::string_view zone_id = {});

} // namespace corundum::dialogue