// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/world/flags.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::quest {
class Registry;
}

namespace corundum::gameplay::dialogue {

/** Describes a condition expression parse failure. */
struct ExprError {
  std::string message;
};

/** Comparison operators accepted by condition expressions. */
enum class CmpOp : uint8_t { Eq, Ne, Lt, Gt, Le, Ge };

/** One node in a compiled condition's node arena.
 *
 *  Children are referenced by arena index (never by pointer), so the arena
 *  vector may grow during compilation without invalidating earlier nodes.
 *
 *  Kind semantics mirror the hand-written evaluator this module replaces:
 *    Int    — integer literal (raw value).
 *    Ident  — flag key, resolved to its visit count (raw value).
 *    Call   — quest/item/reputation/visit helper; raw value (item_count/rep/visits) or 0/1.
 *    Cmp    — comparison of two raw operands; 0/1. When rhs_is_bool and the
 *             operator is ==/!= the operands are compared by truthiness.
 *    And/Or — short-circuit-free boolean combine; operands coerced to truth.
 *    Not    — logical negation of the child value.
 *    Bool   — truthiness coercion (child != 0); emitted where the grammar
 *             treats a bare value as a boolean (a comparison-less operand).
 */
struct ExprNode {
  enum class Kind : uint8_t { Int, Ident, Call, Cmp, And, Or, Not, Bool };

  /** Call: first argument (quest / item / faction / node id). */
  std::string arg = {};
  Kind kind = Kind::Int;

  /** Left operand arena index. */
  int32_t lhs = -1;

  /** Ident: flag key. Call: helper name. */
  std::string name = {};

  /** Cmp: comparison operator. */
  CmpOp op = CmpOp::Eq;

  /** Right operand arena index. */
  int32_t rhs = -1;

  /** Cmp: RHS was a true/false literal. */
  bool rhs_is_bool = false;

  /** Call: second argument (stage name; quest_is_at only). */
  std::string stage_name = {};

  /** Int: literal value. */
  int value = 0;
};

/** Identifiers a compiled condition references, for cross-file validation.
 *
 *  quest_ids holds the quest id of every quest-helper call (including those in
 *  quest_stages). quest_stages additionally pairs a quest with a stage name for
 *  each quest_is_at call, so validators can check the stage exists too.
 */
struct ExprRefs {
  /** Bare flag keys read by the expression. */
  std::vector<std::string> idents;

  /** Quest ids named by quest-helper calls. */
  std::vector<std::string> quest_ids;

  /** (quest, stage) from quest_is_at. */
  std::vector<std::pair<std::string, std::string>> quest_stages;
};

/** A condition expression parsed once into a reusable node arena.
 *
 *  Grammar is unchanged from the original string evaluator: integers, booleans,
 *  bare flag keys, ==/!=/</>/<=/>=, &&/||/!, parentheses, and the quest / item /
 *  reputation helpers plus the node-visit helpers `seen(node_id)` and
 *  `visits(node_id)`. An empty source compiles to an expression that always
 *  evaluates true.
 *
 *  Evaluation never fails — compile() rejects every malformed expression up
 *  front. The original source is kept so content can round-trip through
 *  serialization.
 */
class CompiledExpr {
public:
  CompiledExpr() = default;

  /** Identifiers / quest references read by this expression (deduplicated). */
  [[nodiscard]] ExprRefs refs() const;

  /** Original source text, as passed to compile(). */
  [[nodiscard]] std::string_view source() const noexcept { return source_; }

private:
  friend std::expected<CompiledExpr, ExprError> compile(std::string_view src);
  friend bool evaluate(const CompiledExpr &expr, const corundum::world::FlagStore &vars, const quest::Registry *quests,
                       std::string_view graph_id, std::string_view zone_id);

  std::vector<ExprNode> nodes_;
  std::string source_;
  int32_t root_ = -1;
};

/** Parse a condition expression into a CompiledExpr.
 *
 *  @param src The condition string. Empty compiles to an always-true expression.
 *  @return The compiled expression, or ExprError naming the first parse problem.
 */
[[nodiscard]] std::expected<CompiledExpr, ExprError> compile(std::string_view src);

/** Evaluate a compiled condition against a variable store.
 *
 *  @param expr  A successfully compiled expression.
 *  @param vars  Values resolved via visit_count() (missing = 0).
 *  @param quests Registry used by quest-helper calls; may be nullptr (helpers
 *                that need the registry then evaluate to false).
 *  @param graph_id Owning dialogue graph id, used by `seen`/`visits` to resolve
 *                `_visit_<graph>_<node>` keys. Empty means no graph context, so
 *                the visit helpers evaluate to 0/false.
 *  @param zone_id Current zone; `local.<key>` identifiers resolve to
 *                `zone.<zone_id>.<key>` before lookup. Empty means no scoping.
 *  @return True when the expression holds.
 */
[[nodiscard]] bool evaluate(const CompiledExpr &expr, const corundum::world::FlagStore &vars,
                            const quest::Registry *quests = nullptr, std::string_view graph_id = {},
                            std::string_view zone_id = {});

} // namespace corundum::gameplay::dialogue
