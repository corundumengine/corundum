#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/dialogue/expr.hpp>

namespace corundum::dialogue {

  std::expected<bool, ExprError> eval_condition(std::string_view expr, const corundum::world::FlagStore &vars,
                                                const quest::Registry *quests) {
    return compile(expr).transform([&](const CompiledExpr &compiled) { return evaluate(compiled, vars, quests); });
  }

} // namespace corundum::dialogue