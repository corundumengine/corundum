// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/expr.hpp>

namespace corundum::dialogue {

  std::expected<bool, ExprError> eval_condition(std::string_view expr, const corundum::world::FlagStore &vars,
                                                const quest::Registry *quests, std::string_view zone_id) {
    return compile(expr).transform(
        [&](const CompiledExpr &compiled) { return evaluate(compiled, vars, quests, {}, zone_id); });
  }

} // namespace corundum::dialogue