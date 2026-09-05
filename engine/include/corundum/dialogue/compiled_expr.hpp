#pragma once

#include <corundum/world/flags.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::quest {
  class Registry;
}

namespace corundum::dialogue {

  /** @brief Describes a condition expression parse failure. */
  struct ExprError {
    std::string message;
  };

  /** @brief Comparison operators accepted by condition expressions. */
  enum class CmpOp : uint8_t { Eq, Ne, Lt, Gt, Le, Ge };

  /**
   * @brief One node in a compiled condition's node arena.
   *
   * Children are referenced by arena index (never by pointer), so the arena
   * vector may grow during compilation without invalidating earlier nodes.
   *
   * Kind semantics mirror the hand-written evaluator this module replaces:
   *   Int    — integer literal (raw value).
   *   Ident  — flag key, resolved to its visit count (raw value).
   *   Call   — quest/item/reputation helper; raw value (item_count/rep) or 0/1.
   *   Cmp    — comparison of two raw operands; 0/1. When rhs_is_bool and the
   *            operator is ==/!= the operands are compared by truthiness.
   *   And/Or — short-circuit-free boolean combine; operands coerced to truth.
   *   Not    — logical negation of the child value.
   *   Bool   — truthiness coercion (child != 0); emitted where the grammar
   *            treats a bare value as a boolean (a comparison-less operand).
   */
  struct ExprNode {
    enum class Kind : uint8_t { Int, Ident, Call, Cmp, And, Or, Not, Bool };

    std::string arg = {};  ///< Call: first argument (quest / item / faction id).
    std::string arg2 = {}; ///< Call: second argument (stage name; quest_is_at only).
    int ival = 0;          ///< Int: literal value.
    Kind kind = Kind::Int;
    int32_t lhs = -1;         ///< Left operand arena index.
    std::string name = {};    ///< Ident: flag key. Call: helper name.
    CmpOp op = CmpOp::Eq;     ///< Cmp: comparison operator.
    int32_t rhs = -1;         ///< Right operand arena index.
    bool rhs_is_bool = false; ///< Cmp: RHS was a true/false literal.
  };

  /**
   * @brief Identifiers a compiled condition references, for cross-file validation.
   *
   * quest_ids holds the quest id of every quest-helper call (including those in
   * quest_stages). quest_stages additionally pairs a quest with a stage name for
   * each quest_is_at call, so validators can check the stage exists too.
   */
  struct ExprRefs {
    std::vector<std::string> idents;                               ///< Bare flag keys read by the expression.
    std::vector<std::string> quest_ids;                            ///< Quest ids named by quest-helper calls.
    std::vector<std::pair<std::string, std::string>> quest_stages; ///< (quest, stage) from quest_is_at.
  };

  /**
   * @brief A condition expression parsed once into a reusable node arena.
   *
   * Grammar is unchanged from the original string evaluator: integers, booleans,
   * bare flag keys, ==/!=/</>/<=/>=, &&/||/!, parentheses, and the quest / item /
   * reputation helpers. An empty source compiles to an expression that always
   * evaluates true.
   *
   * Evaluation never fails — compile() rejects every malformed expression up
   * front. The original source is kept so content can round-trip through
   * serialization.
   */
  class CompiledExpr {
  public:
    CompiledExpr() = default;

    /** @brief Identifiers / quest references read by this expression (deduplicated). */
    [[nodiscard]] ExprRefs refs() const;

    /** @brief Original source text, as passed to compile(). */
    [[nodiscard]] std::string_view source() const noexcept {
      return source_;
    }

  private:
    friend std::expected<CompiledExpr, ExprError> compile(std::string_view src);
    friend bool evaluate(const CompiledExpr &expr, const corundum::world::FlagStore &vars,
                         const quest::Registry *quests, std::string_view zone_id);

    std::vector<ExprNode> nodes_;
    std::string source_;
    int32_t root_ = -1;
  };

  /**
   * @brief Parse a condition expression into a CompiledExpr.
   *
   * @param src The condition string. Empty compiles to an always-true expression.
   * @return The compiled expression, or ExprError naming the first parse problem.
   */
  [[nodiscard]] std::expected<CompiledExpr, ExprError> compile(std::string_view src);

  /**
   * @brief Evaluate a compiled condition against a variable store.
   *
   * @param expr  A successfully compiled expression.
   * @param vars  Values resolved via visit_count() (missing = 0).
   * @param quests Registry used by quest-helper calls; may be nullptr (helpers
   *               that need the registry then evaluate to false).
   * @param zone_id Current zone; `local.<key>` identifiers resolve to
   *               `zone.<zone_id>.<key>` before lookup. Empty means no scoping.
   * @return True when the expression holds.
   */
  [[nodiscard]] bool evaluate(const CompiledExpr &expr, const corundum::world::FlagStore &vars,
                              const quest::Registry *quests = nullptr, std::string_view zone_id = {});

} // namespace corundum::dialogue
