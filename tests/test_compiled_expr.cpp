#include <doctest/doctest.h>

#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/world/flags.hpp>

#include <string>

namespace dialogue = corundum::dialogue;
using corundum::world::FlagStore;

namespace {

  // Compiles then evaluates; CHECKs the compile succeeded so a regression in the
  // parser surfaces as a test failure rather than a silent false.
  bool eval_str(std::string_view src, const FlagStore &flags, const corundum::quest::Registry *quests = nullptr) {
    const auto compiled = dialogue::compile(src);
    REQUIRE(compiled.has_value());
    return dialogue::evaluate(*compiled, flags, quests);
  }

  corundum::quest::Registry make_quest_registry() {
    corundum::quest::Registry quests;
    corundum::quest::Quest q;
    q.quest_id = "tq";
    q.name = "TQ";
    q.stages.push_back({"start", 1, false, false, {}});
    q.stages.push_back({"complete", 2, true, false, {}});
    quests.add(std::move(q));
    return quests;
  }

} // namespace

// ── compile → evaluate characterization (port of every eval_condition case) ──

TEST_CASE("compiled_expr: empty source compiles to always-true") {
  FlagStore flags;
  const auto compiled = dialogue::compile("");
  REQUIRE(compiled.has_value());
  CHECK(dialogue::evaluate(*compiled, flags));
}

TEST_CASE("compiled_expr: integer literal truthy/falsy") {
  FlagStore flags;
  CHECK(eval_str("1", flags) == true);
  CHECK(eval_str("0", flags) == false);
  CHECK(eval_str("42", flags) == true);
}

TEST_CASE("compiled_expr: boolean literals") {
  FlagStore flags;
  CHECK(eval_str("true", flags) == true);
  CHECK(eval_str("false", flags) == false);
}

TEST_CASE("compiled_expr: identifier resolves from FlagStore") {
  FlagStore flags;
  corundum::world::set_flag(flags, "paid");

  CHECK(eval_str("paid", flags) == true);
  CHECK(eval_str("other", flags) == false);
}

TEST_CASE("compiled_expr: comparison operators") {
  FlagStore flags;
  flags["gold"] = 10;

  CHECK(eval_str("gold == 10", flags) == true);
  CHECK(eval_str("gold != 10", flags) == false);
  CHECK(eval_str("gold >= 5", flags) == true);
  CHECK(eval_str("gold > 10", flags) == false);
  CHECK(eval_str("gold < 11", flags) == true);
  CHECK(eval_str("gold <= 10", flags) == true);
}

TEST_CASE("compiled_expr: boolean operators") {
  FlagStore flags;
  flags["gold"] = 10;

  CHECK(eval_str("gold >= 5 && gold < 20", flags) == true);
  CHECK(eval_str("gold > 20 || gold == 10", flags) == true);
  CHECK(eval_str("!(gold == 10)", flags) == false);
}

TEST_CASE("compiled_expr: bool comparison uses truthiness") {
  FlagStore flags;
  corundum::world::set_flag(flags, "paid"); // count = 1
  corundum::world::set_flag(flags, "paid"); // count = 2

  CHECK(eval_str("paid == true", flags) == true);
  CHECK(eval_str("paid == false", flags) == false);
}

TEST_CASE("compiled_expr: combined innkeeper condition") {
  FlagStore flags;
  flags["gold"] = 10;

  CHECK(eval_str("gold >= 5 && !paid_innkeeper", flags) == true);

  flags["paid_innkeeper"] = 1;
  CHECK(eval_str("gold >= 5 && !paid_innkeeper", flags) == false);
}

TEST_CASE("compiled_expr: negative literals compare exactly") {
  FlagStore flags;
  flags["rep"] = -3;
  CHECK(eval_str("rep < 0", flags) == true);
  CHECK(eval_str("rep == -3", flags) == true);
  CHECK(eval_str("-3 < 0", flags) == true);
}

TEST_CASE("compiled_expr: parenthesised value used as comparison operand") {
  FlagStore flags;
  flags["gold"] = 3;
  // The paren coerces the bare value to truthiness before the comparison.
  CHECK(eval_str("(gold) == 1", flags) == true);
  CHECK(eval_str("(gold == 3) == 1", flags) == true);
  CHECK(eval_str("(gold) == 3", flags) == false);
}

TEST_CASE("compiled_expr: quest helper quest_is_started works") {
  FlagStore flags;
  const auto quests = make_quest_registry();

  CHECK_FALSE(eval_str("quest_is_started(tq)", flags, &quests));
  flags["quest.tq"] = 1;
  CHECK(eval_str("quest_is_started(tq)", flags, &quests));
}

TEST_CASE("compiled_expr: quest helper quest_is_at works") {
  FlagStore flags;
  const auto quests = make_quest_registry();

  flags["quest.tq"] = 1;
  CHECK(eval_str("quest_is_at(tq, start)", flags, &quests));
  CHECK_FALSE(eval_str("quest_is_at(tq, complete)", flags, &quests));
}

TEST_CASE("compiled_expr: quest helper quest_is_resolved works") {
  FlagStore flags;
  const auto quests = make_quest_registry();

  flags["quest.tq"] = 2;
  CHECK(eval_str("quest_is_resolved(tq)", flags, &quests));
  CHECK_FALSE(eval_str("quest_is_resolved(unknown)", flags, &quests));
}

TEST_CASE("compiled_expr: old quest helper names are compile errors") {
  FlagStore flags;
  const auto quests = make_quest_registry();
  const auto res = dialogue::compile("quest_started(tq)");
  REQUIRE_FALSE(res.has_value());
  CHECK(res.error().message.find("unknown quest helper") != std::string::npos);
}

TEST_CASE("compiled_expr: quest helpers with null registry parse and evaluate false") {
  FlagStore flags;
  CHECK(eval_str("!quest_is_resolved(ember)", flags) == true);
  CHECK_FALSE(eval_str("quest_is_at(ember, done)", flags));
  CHECK_FALSE(eval_str("quest_is_resolved(ember)", flags));
  CHECK_FALSE(eval_str("quest_is_failed(ember)", flags));
}

TEST_CASE("compiled_expr: quest_is_started works without a registry") {
  FlagStore flags;
  flags["quest.ember"] = 1;

  const auto with = dialogue::compile("quest_is_started(ember)");
  REQUIRE(with.has_value());
  CHECK(dialogue::evaluate(*with, flags, nullptr));

  FlagStore empty_flags;
  const auto without = dialogue::compile("quest_is_started(ember)");
  REQUIRE(without.has_value());
  CHECK_FALSE(dialogue::evaluate(*without, empty_flags, nullptr));
}

TEST_CASE("compiled_expr: has_item / item_count / rep helpers") {
  FlagStore flags;
  flags["item.hammer"] = 2;
  flags["rep.village"] = 5;

  CHECK(eval_str("has_item(hammer)", flags) == true);
  CHECK(eval_str("has_item(sword)", flags) == false);

  CHECK(eval_str("item_count(hammer) >= 2", flags) == true);
  CHECK(eval_str("item_count(hammer) >= 3", flags) == false);
  CHECK(eval_str("item_count(hammer) == 2", flags) == true);

  CHECK(eval_str("rep(village) >= 5", flags) == true);
  CHECK(eval_str("rep(village) > 5", flags) == false);
  CHECK(eval_str("rep(other) >= 1", flags) == false);

  CHECK(eval_str("has_item(hammer) && rep(village) >= 3", flags) == true);
  CHECK(eval_str("has_item(sword) || rep(village) >= 3", flags) == true);
}

// ── Malformed input ─────────────────────────────────────────────────────────

TEST_CASE("compiled_expr: malformed expressions fail to compile") {
  const std::vector<std::string> bad = {
      "gold >=",                     // dangling comparison
      "5 ===",                       // stray operator
      "quest_is_at(ember)",          // missing stage arg
      "quest_is_at(ember, done, x)", // too many args
      "quest_is_at(ember, 5)",       // stage must be an identifier
      "quest_unknown(x)",            // unknown helper
      "has_item('hammer')",          // strings not allowed in conditions
      "!",                           // dangling not
      "gold &&",                     // dangling and
      "foo(",                        // unterminated call
      "   ",                         // whitespace only
  };
  for (const auto &src : bad) {
    CAPTURE(src);
    CHECK_FALSE(dialogue::compile(src).has_value());
  }
}

TEST_CASE("compiled_expr: compile error carries a non-empty message") {
  const auto res = dialogue::compile("gold >=");
  REQUIRE_FALSE(res.has_value());
  CHECK_FALSE(res.error().message.empty());
}

// ── refs() ───────────────────────────────────────────────────────────────────

TEST_CASE("compiled_expr: refs collects identifiers") {
  const auto compiled = dialogue::compile("gold >= 5 && paid");
  REQUIRE(compiled.has_value());
  const auto refs = compiled->refs();
  REQUIRE(refs.idents.size() == 2);
  CHECK(refs.idents[0] == "gold");
  CHECK(refs.idents[1] == "paid");
  CHECK(refs.quest_ids.empty());
  CHECK(refs.quest_stages.empty());
}

TEST_CASE("compiled_expr: refs collects quest ids and stages") {
  const auto compiled =
      dialogue::compile("quest_is_at(ember_of_greyhollow, cave_entered) && quest_is_started(ember_of_greyhollow)");
  REQUIRE(compiled.has_value());
  const auto refs = compiled->refs();
  REQUIRE(refs.quest_ids.size() == 1);
  CHECK(refs.quest_ids[0] == "ember_of_greyhollow");
  REQUIRE(refs.quest_stages.size() == 1);
  CHECK(refs.quest_stages[0] == std::make_pair("ember_of_greyhollow", "cave_entered"));
}

TEST_CASE("compiled_expr: refs dedupes repeated references") {
  const auto compiled = dialogue::compile("quest_is_at(ember, done) || quest_is_at(ember, done)");
  REQUIRE(compiled.has_value());
  const auto refs = compiled->refs();
  CHECK(refs.quest_ids.size() == 1);
  CHECK(refs.quest_stages.size() == 1);
}

TEST_CASE("compiled_expr: item and rep helpers are not quest refs") {
  const auto compiled = dialogue::compile("has_item(hammer) && rep(village) >= 1");
  REQUIRE(compiled.has_value());
  const auto refs = compiled->refs();
  CHECK(refs.quest_ids.empty());
  CHECK(refs.quest_stages.empty());
}

// ── source round-trip ───────────────────────────────────────────────────────

TEST_CASE("compiled_expr: source is preserved for serialization") {
  const auto compiled = dialogue::compile("gold >= 5 && !paid");
  REQUIRE(compiled.has_value());
  CHECK(compiled->source() == "gold >= 5 && !paid");

  const auto empty = dialogue::compile("");
  REQUIRE(empty.has_value());
  CHECK(empty->source().empty());
}