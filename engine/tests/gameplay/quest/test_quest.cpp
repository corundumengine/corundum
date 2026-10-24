// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/quest/quest.hpp>
#include <cstddef>
#include <doctest/doctest.h>

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/dialogue/compiled_expr.hpp>
#include <corundum/gameplay/quest/loader.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/runner.hpp>
#include <corundum/gameplay/quest/serialize.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/world/flags.hpp>

#include "memory_content_source.hpp"
#include <cstdint>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using corundum::test::disk;

namespace quest = corundum::gameplay::quest;
using corundum::world::FlagStore;

namespace {

  quest::Quest make_test_quest() {
    quest::Quest q;
    q.quest_id = "test_quest";
    q.name = "Test Quest";
    q.description = "A test quest.";

    q.stages.push_back({.failed = false, .name = "start", .objectives = {}, .resolved = false, .sequence = 1});
    q.stages.push_back({.failed = false, .name = "middle", .objectives = {}, .resolved = false, .sequence = 2});
    q.stages.push_back({.failed = false, .name = "complete", .objectives = {}, .resolved = true, .sequence = 3});
    q.stages.push_back({.failed = true, .name = "failed", .objectives = {}, .resolved = true, .sequence = 4});

    return q;
  }

  quest::Quest make_quest_with_objectives() {
    quest::Quest q;
    q.quest_id = "obj_quest";
    q.name = "Obj Quest";
    q.description = "A quest with journal objectives.";

    q.stages.push_back({
        .name = "start",
        .objectives =
            {
                {.done_condition = std::nullopt, .text = "Bare objective"},
                {
                    .done_condition = *corundum::gameplay::dialogue::compile("ember_tracks_found >= 1"),
                    .text = "Conditioned objective",
                },
            },
        .sequence = 1,
    });
    q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});

    return q;
  }

  /// A single-stage quest whose sole objective is auto-checked by @p condition.
  quest::Quest make_quest_with_condition(std::string_view condition) {
    quest::Quest q;
    q.quest_id = "cond_quest";
    q.name = "Conditional Quest";
    q.description = "A quest driven by a done_condition.";

    q.stages.push_back({
        .name = "start",
        .objectives = {{.done_condition = *corundum::gameplay::dialogue::compile(condition), .text = "Conditioned"}},
        .sequence = 1,
    });
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});

    return q;
  }

  /// Condition source text for an objective, or empty when it carries no condition.
  std::string_view condition_source(const quest::Objective &objective) {
    return objective.done_condition.has_value() ? objective.done_condition->source() : std::string_view{};
  }

  void check_objective_matches(const quest::Objective &actual, const quest::Objective &expected) {
    CHECK(actual.text == expected.text);
    CHECK(actual.done_condition.has_value() == expected.done_condition.has_value());
    CHECK(condition_source(actual) == condition_source(expected));
  }

  void check_objectives_match(const std::vector<quest::Objective> &actual,
                              const std::vector<quest::Objective> &expected) {
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
      check_objective_matches(actual[i], expected[i]);
  }

  void check_stage_name_matches(const quest::Stage &actual, const quest::Stage &expected) {
    CHECK(actual.name == expected.name);
    CHECK(actual.sequence == expected.sequence);
  }

  void check_stage_flags_match(const quest::Stage &actual, const quest::Stage &expected) {
    CHECK(actual.resolved == expected.resolved);
    CHECK(actual.failed == expected.failed);
  }

  void check_stage_identity_matches(const quest::Stage &actual, const quest::Stage &expected) {
    check_stage_name_matches(actual, expected);
    check_stage_flags_match(actual, expected);
  }

  void check_stage_edges_match(const quest::Stage &actual, const quest::Stage &expected) {
    CHECK(actual.advances_to == expected.advances_to);
    CHECK(actual.auto_advance_to == expected.auto_advance_to);
  }

  void check_stages_match(const quest::Stage &actual, const quest::Stage &expected) {
    check_stage_identity_matches(actual, expected);
    check_stage_edges_match(actual, expected);
    check_objectives_match(actual.objectives, expected.objectives);
  }

  void check_quest_identity_matches(const quest::Quest &actual, const quest::Quest &expected) {
    CHECK(actual.quest_id == expected.quest_id);
    CHECK(actual.name == expected.name);
    CHECK(actual.description == expected.description);
  }

  void check_quests_match(const quest::Quest &actual, const quest::Quest &expected) {
    check_quest_identity_matches(actual, expected);

    REQUIRE(actual.stages.size() == expected.stages.size());
    for (std::size_t i = 0; i < expected.stages.size(); ++i)
      check_stages_match(actual.stages[i], expected.stages[i]);
  }

  /// A quest with no auto_advance_to must stay on its current stage across a tick.
  void check_quest_stays_put(const quest::Registry &registry, const quest::Quest &quest) {
    for (const auto &stage : quest.stages)
      CHECK_FALSE(stage.auto_advance_to.has_value());

    FlagStore flags;
    flags[quest::quest_flag_key(quest.quest_id)] = quest.stages[0].sequence;
    quest::tick_quests(registry, flags, {});
    CHECK(quest::get_stage(quest.quest_id, flags) == quest.stages[0].sequence);
  }

} // namespace

TEST_CASE("quest loader: malformed done_condition is a load error") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/bad_done_condition.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[{"text":"t","done_condition":"gold >="}]}
    ]})");

  auto result = quest::load_quest(content, path);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("done_condition invalid") != std::string::npos);
}

TEST_CASE("quest loader: empty done_condition is a load error") {
  // An empty expression compiles to constant-true, so accepting it would silently
  // complete the objective (and auto-advance its stage).
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/empty_done_condition.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[{"text":"t","done_condition":""}]}
    ]})");

  const auto result = quest::load_quest(content, path);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("done_condition") != std::string::npos);
}

TEST_CASE("quest loader: valid JSON produces correct Quest struct") {
  const auto result = quest::load_quest(disk(), "engine/tests/fixtures/find_sword.json");
  REQUIRE(result.has_value());
  const auto &q = *result;

  CHECK(q.quest_id == "find_sword");
  CHECK(q.name == "The Lost Sword");
  CHECK(q.description == "A blade of legend, lost in the old dungeon.");
  REQUIRE(q.stages.size() == 4);

  CHECK(q.stages[0].name == "start");
  CHECK(q.stages[0].sequence == 1);
  CHECK_FALSE(q.stages[0].resolved);

  CHECK(q.stages[1].name == "return");
  CHECK(q.stages[1].sequence == 2);

  CHECK(q.stages[2].name == "complete_helped");
  CHECK(q.stages[2].sequence == 3);
  CHECK(q.stages[2].resolved);
  CHECK_FALSE(q.stages[2].failed);

  CHECK(q.stages[3].name == "complete_betrayed");
  CHECK(q.stages[3].sequence == 4);
  CHECK(q.stages[3].resolved);
  CHECK_FALSE(q.stages[3].failed);
}

TEST_CASE("quest loader: missing type field loads") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/no_type.json";
  content.add(path, R"({"id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE(result.has_value());
  CHECK(result->quest_id == "x");
}

TEST_CASE("quest loader: wrong type field warns but loads") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/wrong_type.json";
  content.add(path, R"({"type":"dialogue","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE(result.has_value());
  CHECK(result->quest_id == "x");
}

TEST_CASE("quest loader: missing id fails") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quest_no_id.json";
  content.add(
      path,
      R"({"type":"quest","name":"x","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE_FALSE(result.has_value());
  const std::string &error = result.error();
  CHECK_MESSAGE(error.starts_with(std::format("[schema] {}: ", path)), error);
  CHECK_MESSAGE(error.find("required property 'id' not found") != std::string::npos, error);
}

TEST_CASE("quest loader: empty id fails") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quest_empty_id.json";
  content.add(
      path,
      R"({"type":"quest","id":"","name":"x","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE_FALSE(result.has_value());
  const std::string &error = result.error();
  CHECK_MESSAGE(error.starts_with(std::format("[schema] {}: ", path)), error);
  CHECK_MESSAGE(error.find("At /id ") != std::string::npos, error);
}

TEST_CASE("quest loader: duplicate stage sequences rejected") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quest_dup_seq.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[]},
      {"name":"b","sequence":1,"resolved":true,"objectives":[]}
    ]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::string{R"([quest] "x": duplicate stage sequence 1)"});
}

TEST_CASE("quest loader: sequence <= 0 rejected") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quest_bad_seq.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":0,"resolved":true,"objectives":[]}
    ]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE_FALSE(result.has_value());
  const std::string &error = result.error();
  CHECK_MESSAGE(error.starts_with(std::format("[schema] {}: ", path)), error);
  CHECK_MESSAGE(error.find("At /stages/0/sequence ") != std::string::npos, error);
}

TEST_CASE("quest loader: no resolved stage rejected") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quest_no_resolved.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[]}
    ]})");

  const auto result = quest::load_quest(content, path);
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), std::string{R"([quest] "x" has no resolved stage)"});
}

TEST_CASE("quest loader: stage order disagreeing with sequences warns but loads") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/stage_order.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"later","sequence":2,"resolved":true,"objectives":[]},
      {"name":"earlier","sequence":1,"objectives":[]}
    ]})");

  auto result = quest::load_quest(content, path);
  REQUIRE(result.has_value());
  CHECK(result->stages[0].name == "later");
  CHECK(result->stages[1].name == "earlier");
}

TEST_CASE("quest loader: failed flag auto-sets resolved") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/failed_flag.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"fail","sequence":1,"failed":true,"objectives":[]}
    ]})");

  auto result = quest::load_quest(content, path);
  REQUIRE(result.has_value());
  CHECK(result->stages[0].failed);
  CHECK(result->stages[0].resolved);
}

TEST_CASE("quest loader: advances_to parses and round-trips") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/advances_to.json";
  content.add(path, R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"advances_to":["b","c"],"objectives":[]},
      {"name":"b","sequence":2,"resolved":true,"objectives":[]},
      {"name":"c","sequence":3,"resolved":true,"objectives":[]}
    ]})");

  auto result = quest::load_quest(content, path);
  REQUIRE(result.has_value());
  REQUIRE(result->stages[0].advances_to.size() == 2);
  CHECK(result->stages[0].advances_to[0] == "b");
  CHECK(result->stages[0].advances_to[1] == "c");
  CHECK(result->stages[1].advances_to.empty());

  const auto j = quest::serialize(*result);
  corundum::test::MemoryContentSource reloaded_content;
  const std::string reloaded_path = "quests/advances_to_reloaded.json";
  reloaded_content.add(reloaded_path, corundum::core::format_json(j));

  const auto reloaded = quest::load_quest(reloaded_content, reloaded_path);
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->stages[0].advances_to.size() == 2);
  CHECK(reloaded->stages[0].advances_to[0] == "b");
  CHECK(reloaded->stages[0].advances_to[1] == "c");
}

TEST_CASE("quest loader: keystone ember_of_greyhollow still loads clean") {
  const auto path = std::filesystem::path("../keystone/data/quests/ember_of_greyhollow.json");
  if (!std::filesystem::exists(path)) {
    MESSAGE("keystone checkout not present; skipping");
    return;
  }
  const auto result = quest::load_quest(disk(), path);
  REQUIRE(result.has_value());
  CHECK(result->quest_id == "ember_of_greyhollow");
  CHECK_FALSE(result->stages.empty());
}

TEST_CASE("quest loader: invalid JSON returns error") {
  corundum::test::MemoryContentSource content;
  const std::string path = "quests/bad_json.json";
  content.add(path, "not valid json");

  auto result = quest::load_quest(content, path);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("malformed quest JSON") != std::string::npos);
  CHECK(result.error().find(path) != std::string::npos);
}

TEST_CASE("validate: valid quest returns empty vector") {
  const auto q = make_test_quest();
  const auto validation = quest::validate(q);
  CHECK(validation.errors.empty());
  CHECK(validation.ok());
}

TEST_CASE("validate: empty quest_id is rejected") {
  quest::Quest q;
  q.stages.push_back({.name = "a", .resolved = true, .sequence = 1});
  const auto validation = quest::validate(q);
  CHECK_FALSE(validation.ok());
  CHECK(validation.errors.front().find("empty quest_id") != std::string::npos);
}

TEST_CASE("validate: empty quest name is rejected") {
  auto q = make_test_quest();
  q.name.clear();
  const auto validation = quest::validate(q);
  CHECK_FALSE(validation.ok());
  CHECK(validation.errors.front().find("empty name") != std::string::npos);
}

TEST_CASE("validate: empty stage name is rejected") {
  auto q = make_test_quest();
  q.stages[0].name.clear();
  const auto validation = quest::validate(q);
  CHECK_FALSE(validation.ok());
  CHECK(validation.errors.front().find("empty name") != std::string::npos);
}

TEST_CASE("validate: duplicate stage names") {
  auto q = make_test_quest();
  q.stages.push_back({.failed = false, .name = "start", .objectives = {}, .resolved = true, .sequence = 5});
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("duplicate stage name") != std::string::npos);
}

TEST_CASE("validate: duplicate sequences") {
  auto q = make_test_quest();
  q.stages.push_back({.failed = false, .name = "unique_name", .objectives = {}, .resolved = true, .sequence = 2});
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("duplicate stage sequence") != std::string::npos);
}

TEST_CASE("validate: duplicate sequence is not also reported as an order warning") {
  quest::Quest q;
  q.quest_id = "dup_warn";
  q.name = "Dup Warn";
  q.stages.push_back({.name = "a", .resolved = false, .sequence = 1});
  q.stages.push_back({.name = "b", .resolved = true, .sequence = 1});
  const auto validation = quest::validate(q);
  REQUIRE_FALSE(validation.errors.empty());
  CHECK(validation.errors.front().find("duplicate stage sequence") != std::string::npos);
  CHECK(validation.warnings.empty());
}

TEST_CASE("validate: no resolved stage") {
  quest::Quest q;
  q.quest_id = "unresolved";
  q.name = "Unresolved";
  q.stages.push_back({.failed = false, .name = "a", .objectives = {}, .resolved = false, .sequence = 1});
  q.stages.push_back({.failed = false, .name = "b", .objectives = {}, .resolved = false, .sequence = 2});
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("no resolved stage") != std::string::npos);
}

TEST_CASE("validate: non-positive stage sequences are rejected") {
  auto q = make_test_quest();

  // Zero collides with the "not started" sentinel get_stage() reports.
  q.stages[1].sequence = 0;
  const std::vector<std::string> zero_errors = quest::validate(q).errors;
  REQUIRE_FALSE(zero_errors.empty());
  CHECK(zero_errors.front().find("must be positive") != std::string::npos);

  q.stages[1].sequence = -3;
  const std::vector<std::string> negative_errors = quest::validate(q).errors;
  REQUIRE_FALSE(negative_errors.empty());
  CHECK(negative_errors.front().find("must be positive") != std::string::npos);
}

TEST_CASE("validate: failed stage that is not resolved is rejected") {
  quest::Quest q;
  q.quest_id = "unnormalized";
  q.name = "Unnormalized";
  q.stages.push_back({.failed = false, .name = "end", .objectives = {}, .resolved = true, .sequence = 1});
  q.stages.push_back({.failed = true, .name = "bad_end", .objectives = {}, .resolved = false, .sequence = 2});

  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("failed but not resolved") != std::string::npos);
}

TEST_CASE("validate: unknown advances_to target is an error") {
  auto q = make_test_quest();
  q.stages[0].advances_to.emplace_back("no_such_stage");
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("advances_to") != std::string::npos);
}

TEST_CASE("validate: known advances_to targets pass") {
  auto q = make_test_quest();
  q.stages[0].advances_to = {"middle"};
  CHECK(quest::validate(q).errors.empty());
}

TEST_CASE("validate: out-of-order sequences surface as a warning, not an error") {
  quest::Quest q;
  q.quest_id = "desync";
  q.name = "Desync";
  q.description = "";
  q.stages.push_back({.failed = false, .name = "start", .objectives = {}, .resolved = false, .sequence = 1});
  q.stages.push_back({.failed = false, .name = "complete", .objectives = {}, .resolved = true, .sequence = 3});
  q.stages.push_back({.failed = false, .name = "middle", .objectives = {}, .resolved = false, .sequence = 2});

  const quest::ValidationResult validation = quest::validate(q);
  CHECK(validation.errors.empty());
  REQUIRE_FALSE(validation.warnings.empty());
  CHECK(validation.warnings.front().find("sequence") != std::string::npos);
}

TEST_CASE("validate: in-order quest produces no warnings") {
  const auto q = make_test_quest();
  const quest::ValidationResult validation = quest::validate(q);
  CHECK(validation.errors.empty());
  CHECK(validation.warnings.empty());
}

TEST_CASE("Quest::find_stage returns correct stage for known name") {
  const auto q = make_test_quest();
  const auto *s = q.find_stage("middle");
  REQUIRE(s != nullptr);
  CHECK(s->name == "middle");
  CHECK(s->sequence == 2);
}

TEST_CASE("Quest::find_stage returns nullptr for unknown name") {
  const auto q = make_test_quest();
  CHECK(q.find_stage("does_not_exist") == nullptr);
}

TEST_CASE("start sets quest.{id} to first stage sequence") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  CHECK(corundum::world::visit_count(flags, "quest.test_quest") == 1);
}

TEST_CASE("start is no-op when already started") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 2;

  quest::start(q, flags);
  CHECK(flags["quest.test_quest"] == 2);
}

TEST_CASE("start on a quest with no stages is a no-op") {
  quest::Quest q;
  q.quest_id = "empty";
  FlagStore flags;

  quest::start(q, flags);
  CHECK(flags.empty());
}

TEST_CASE("start on a quest whose first stage has a non-positive sequence is a no-op") {
  quest::Quest q;
  q.quest_id = "bad_seq";
  q.name = "Bad";
  q.stages.push_back({.name = "a", .resolved = true, .sequence = 0});
  FlagStore flags;

  quest::start(q, flags);
  CHECK(flags.empty());
}

TEST_CASE("advance moves to correct stage sequence") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 1;

  quest::advance(q, "middle", flags);
  CHECK(flags["quest.test_quest"] == 2);
}

TEST_CASE("advance works on unstarted quest") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  CHECK(flags["quest.test_quest"] == 2);
}

TEST_CASE("advance is no-op for unknown stage name") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 1;

  // Should not throw, just warn
  quest::advance(q, "does_not_exist", flags);
  CHECK(flags["quest.test_quest"] == 1);
}

TEST_CASE("advance: transition outside advances_to still happens (advisory only)") {
  quest::Quest q;
  q.quest_id = "edge_q";
  q.name = "Edge Q";
  q.description = "";
  q.stages.push_back({.failed = false, .name = "a", .objectives = {}, .resolved = false, .sequence = 1});
  q.stages.push_back({.failed = false, .name = "b", .objectives = {}, .resolved = false, .sequence = 2});
  q.stages.push_back({.failed = false, .name = "c", .objectives = {}, .resolved = true, .sequence = 3});
  q.stages[0].advances_to = {"b"};

  FlagStore flags;
  flags["quest.edge_q"] = 1;

  // "c" is not listed in stage "a" advances_to. A debug build prints a warning,
  // but the transition still occurs — advances_to is advisory, not enforced.
  quest::advance(q, "c", flags);
  CHECK(flags["quest.edge_q"] == 3);
}

TEST_CASE("advance: auto_advance_to target is exempt from the advances_to warning") {
  quest::Quest q;
  q.quest_id = "auto_q";
  q.name = "Auto Q";
  q.description = "";
  q.stages.push_back({.failed = false, .name = "a", .objectives = {}, .resolved = false, .sequence = 1});
  q.stages.push_back({.failed = false, .name = "b", .objectives = {}, .resolved = false, .sequence = 2});
  q.stages.push_back({.failed = false, .name = "c", .objectives = {}, .resolved = true, .sequence = 3});
  q.stages[0].advances_to = {"b"};
  q.stages[0].auto_advance_to = "c";

  FlagStore flags;
  flags["quest.auto_q"] = 1;

  // "c" is the stage's auto_advance_to — legal even though not in advances_to.
  quest::advance(q, "c", flags);
  CHECK(flags["quest.auto_q"] == 3);
}

TEST_CASE("get_stage reads flag correctly") {
  FlagStore flags;
  flags["quest.test_quest"] = 3;

  CHECK(quest::get_stage("test_quest", flags) == 3);
}

TEST_CASE("get_stage returns 0 when key absent") {
  FlagStore flags;
  CHECK(quest::get_stage("test_quest", flags) == 0);
}

TEST_CASE("is_resolved true when flag matches any resolved stage") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3; // "complete" is resolved

  CHECK(quest::is_resolved(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("is_resolved false when inactive or on non-resolved stage") {
  const auto q = make_test_quest();
  FlagStore flags;

  // Inactive
  CHECK_FALSE(quest::is_resolved(q, flags));

  // Non-resolved stage
  flags["quest.test_quest"] = 1;
  CHECK_FALSE(quest::is_resolved(q, flags));
}

TEST_CASE("multiple resolved stages both satisfy is_resolved") {
  const auto q = make_test_quest();
  FlagStore flags;

  flags["quest.test_quest"] = 3;
  CHECK(quest::is_resolved(q, flags));

  flags["quest.test_quest"] = 4;
  CHECK(quest::is_resolved(q, flags));
}

TEST_CASE("is_failed true when flag matches failed stage") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 4; // "failed" is failed+resolved

  CHECK(quest::is_resolved(q, flags));
  CHECK(quest::is_failed(q, flags));
}

TEST_CASE("is_failed false for non-failed resolved stage") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3; // "complete" is resolved but not failed

  CHECK(quest::is_resolved(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("is_failed false when not started") {
  const auto q = make_test_quest();
  FlagStore flags;

  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("started_quests returns every quest with stage > 0, including resolved ones") {
  corundum::test::MemoryContentSource content;
  content.add("started/q1.json", R"({"type":"quest","id":"q1","name":"Q1","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}
    ]})");
  content.add("started/q2.json", R"({"type":"quest","id":"q2","name":"Q2","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}
    ]})");

  quest::Registry reg;
  const auto n_loaded = reg.load_all(content, "started");
  CHECK(n_loaded == 2);
  CHECK(reg.size() == 2);

  FlagStore flags;
  flags["quest.q1"] = 1; // q1 started on a resolved stage
  // q2 not started

  auto started = quest::started_quests(reg, flags);
  REQUIRE(started.size() == 1);
  CHECK(started[0]->quest_id == "q1");
  // "started" includes resolved quests; it is not the same as Lifecycle::Active.
  CHECK(quest::lifecycle(*started[0], flags) == quest::Lifecycle::Completed);
}

TEST_CASE("registry load_all count matches size with duplicate ids") {
  corundum::test::MemoryContentSource content;
  content.add(
      "dup_count/a.json",
      R"({"type":"quest","id":"dup","name":"A","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})");
  content.add(
      "dup_count/b.json",
      R"({"type":"quest","id":"dup","name":"B","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})");

  quest::Registry reg;
  const int loaded = reg.load_all(content, "dup_count");
  CHECK(loaded == 1);
  CHECK(reg.size() == 1);
}

TEST_CASE("registry load_all loads valid files, skips bad ones") {
  const auto find_sword = disk().read("engine/tests/fixtures/find_sword.json");
  REQUIRE(find_sword.has_value());

  corundum::test::MemoryContentSource content;
  content.add("registry/find_sword.json", std::span<const std::uint8_t>{*find_sword});
  content.add("registry/bad.json", R"({"type":"quest"})"); // missing required fields
  content.add("registry/notes.txt", "not a quest");        // non-JSON file

  quest::Registry reg;
  const int loaded = reg.load_all(content, "registry");

  CHECK(loaded == 1);
  CHECK(reg.size() == 1);

  const auto *q = reg.find("find_sword");
  REQUIRE(q != nullptr);
  CHECK(q->quest_id == "find_sword");

  CHECK(reg.find("does_not_exist") == nullptr);
}

TEST_CASE("registry add returns false for a duplicate id and keeps the first quest") {
  quest::Registry reg;

  auto first = make_test_quest();
  first.name = "First";
  CHECK(reg.add(std::move(first)));

  auto second = make_test_quest();
  second.name = "Second";
  CHECK_FALSE(reg.add(std::move(second)));

  CHECK(reg.size() == 1);
  const auto *q = reg.find("test_quest");
  REQUIRE(q != nullptr);
  CHECK(q->name == "First");
}

TEST_CASE("registry add and find are heterogeneously keyed") {
  quest::Registry reg;
  CHECK(reg.add(make_test_quest()));

  // A string_view lookup must find the entry owned by the registry's string key.
  const std::string_view key = "test_quest";
  const auto *q = reg.find(key);
  REQUIRE(q != nullptr);
  CHECK(q->quest_id == "test_quest");
}

TEST_CASE("lifecycle: helped path") {
  const auto q = make_test_quest();
  FlagStore flags;

  // Not started
  CHECK(quest::get_stage("test_quest", flags) == 0);

  // Start
  quest::start(q, flags);
  CHECK(quest::get_stage("test_quest", flags) == 1);
  CHECK_FALSE(quest::is_resolved(q, flags));

  // Advance to middle
  quest::advance(q, "middle", flags);
  CHECK(quest::get_stage("test_quest", flags) == 2);
  CHECK_FALSE(quest::is_resolved(q, flags));

  // Advance to resolved
  quest::advance(q, "complete", flags);
  CHECK(quest::get_stage("test_quest", flags) == 3);
  CHECK(quest::is_resolved(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("lifecycle: betrayed path") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  quest::advance(q, "failed", flags);
  CHECK(quest::get_stage("test_quest", flags) == 4);
  CHECK(quest::is_resolved(q, flags));
  CHECK(quest::is_failed(q, flags));
}

TEST_CASE("lifecycle: organic discovery") {
  const auto q = make_test_quest();
  FlagStore flags;

  // Advance without start — works via organic discovery
  quest::advance(q, "middle", flags);
  CHECK(quest::get_stage("test_quest", flags) == 2);
}

TEST_CASE("lifecycle: all four states map to the right enum") {
  const auto q = make_test_quest();
  FlagStore flags;

  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::NotStarted);

  flags["quest.test_quest"] = 1;
  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::Active);

  flags["quest.test_quest"] = 2;
  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::Active);

  flags["quest.test_quest"] = 3; // "complete" is resolved but not failed
  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::Completed);

  flags["quest.test_quest"] = 4; // "failed" is resolved + failed
  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::Failed);
}

TEST_CASE("current_stage returns nullptr when quest is not started") {
  const auto q = make_test_quest();
  FlagStore flags;
  CHECK(quest::current_stage(q, flags) == nullptr);
}

TEST_CASE("current_stage matches the active flag") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 2;
  const auto *stage = quest::current_stage(q, flags);
  REQUIRE(stage != nullptr);
  CHECK(stage->name == "middle");
}

TEST_CASE("current_stage points at the resolved ending stage") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3;
  const auto *stage = quest::current_stage(q, flags);
  REQUIRE(stage != nullptr);
  CHECK(stage->name == "complete");
}

TEST_CASE("current_stage returns nullptr for a sequence with no stage") {
  const auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 99;
  CHECK(quest::current_stage(q, flags) == nullptr);

  // A dangling sequence still reports Active — the stage itself is the only way to tell.
  CHECK(quest::lifecycle(q, flags) == quest::Lifecycle::Active);
  CHECK(quest::objectives(q, flags).empty());
}

TEST_CASE("objectives returns nothing for an unstarted quest") {
  const auto q = make_quest_with_objectives();
  FlagStore flags;
  CHECK(quest::objectives(q, flags).empty());
}

TEST_CASE("objectives reports bare and conditioned objectives") {
  const auto q = make_quest_with_objectives();
  FlagStore flags;
  flags["quest.obj_quest"] = 1;

  const auto views = quest::objectives(q, flags);
  REQUIRE(views.size() == 2);

  CHECK(views[0].text == "Bare objective");
  CHECK_FALSE(views[0].has_condition);
  CHECK_FALSE(views[0].done);

  CHECK(views[1].text == "Conditioned objective");
  CHECK(views[1].has_condition);
  CHECK_FALSE(views[1].done);
}

TEST_CASE("objectives marks a conditioned objective done when its flag is set") {
  const auto q = make_quest_with_objectives();
  FlagStore flags;
  flags["quest.obj_quest"] = 1;
  flags["ember_tracks_found"] = 1;

  const auto views = quest::objectives(q, flags);
  REQUIRE(views.size() == 2);
  CHECK_FALSE(views[0].done);
  CHECK(views[1].done);
}

TEST_CASE("objectives resolves a quest-helper condition through the registry") {
  const auto q = make_quest_with_condition("quest_is_resolved(test_quest)");
  quest::Registry registry;
  registry.add(make_test_quest());
  FlagStore flags;
  flags["quest.cond_quest"] = 1;
  flags["quest.test_quest"] = 3; // test_quest is on its resolved stage

  const auto with_registry = quest::objectives(q, flags, &registry);
  REQUIRE(with_registry.size() == 1);
  CHECK(with_registry[0].done);

  // Without the registry the helper has no quest to resolve, so it stays false.
  const auto without_registry = quest::objectives(q, flags);
  REQUIRE(without_registry.size() == 1);
  CHECK_FALSE(without_registry[0].done);
}

TEST_CASE("objectives resolves a local.<key> condition against the zone") {
  const auto q = make_quest_with_condition("local.ember_tracks_found >= 1");
  FlagStore flags;
  flags["quest.cond_quest"] = 1;
  flags["zone.ember_marsh.ember_tracks_found"] = 1;

  const auto scoped = quest::objectives(q, flags, nullptr, "ember_marsh");
  REQUIRE(scoped.size() == 1);
  CHECK(scoped[0].done);

  // With no active zone the local key does not resolve, so the condition is false.
  const auto unscoped = quest::objectives(q, flags);
  REQUIRE(unscoped.size() == 1);
  CHECK_FALSE(unscoped[0].done);
}

TEST_CASE("start records the quest.<id>.seen.<stage> breadcrumb") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.start"));
}

TEST_CASE("advance records a breadcrumb for each stage entered") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.middle"));

  quest::advance(q, "complete", flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.complete"));
  // Breadcrumb from an earlier stage remains.
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.middle"));
}

TEST_CASE("advance to an unknown stage records no breadcrumb") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "does_not_exist", flags);
  CHECK_FALSE(corundum::world::has_flag(flags, "quest.test_quest.seen.does_not_exist"));
}

TEST_CASE("advance to a stage already entered re-records the breadcrumb") {
  const auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  quest::advance(q, "middle", flags);
  CHECK(corundum::world::visit_count(flags, "quest.test_quest.seen.middle") == 2);
}

TEST_CASE("quest_flag_key formats correctly") {
  CHECK(quest::quest_flag_key("find_sword") == "quest.find_sword");
  CHECK(quest::quest_flag_key("escort_merchant") == "quest.escort_merchant");
  CHECK(quest::quest_flag_key("") == "quest.");
}

TEST_CASE("Runner: start on an unknown id returns an error and sets no flag") {
  const quest::Registry registry;
  FlagStore flags;
  quest::Runner runner{registry, flags};

  const auto result = runner.start("bogus");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "quest_start(\"bogus\") references unknown quest");
  CHECK(flags.empty());
}

TEST_CASE("Runner: start on a known id sets the quest flag to the first stage") {
  quest::Registry registry;
  registry.add(make_test_quest());
  FlagStore flags;
  quest::Runner runner{registry, flags};

  REQUIRE(runner.start("test_quest").has_value());
  CHECK(quest::get_stage("test_quest", flags) == 1);
}

TEST_CASE("Runner: advance on an unknown id returns an error") {
  const quest::Registry registry;
  FlagStore flags;
  quest::Runner runner{registry, flags};

  const auto result = runner.advance("bogus", "middle");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "quest_advance(\"bogus\", \"middle\") references unknown quest");
}

TEST_CASE("Runner: advance on a known id moves the flag to the named stage") {
  quest::Registry registry;
  registry.add(make_test_quest()); // stage "middle" has sequence 2
  FlagStore flags;
  quest::Runner runner{registry, flags};

  REQUIRE(runner.advance("test_quest", "middle").has_value());
  CHECK(quest::get_stage("test_quest", flags) == 2);
}

TEST_CASE("Runner: start on an already-underway quest returns ok without moving the flag") {
  quest::Registry registry;
  registry.add(make_test_quest()); // stage "middle" has sequence 2
  FlagStore flags;
  quest::Runner runner{registry, flags};

  REQUIRE(runner.advance("test_quest", "middle").has_value());
  REQUIRE(runner.start("test_quest").has_value());
  CHECK(quest::get_stage("test_quest", flags) == 2);
}

TEST_CASE("Runner: advance to a stage the quest does not define returns ok without moving the flag") {
  quest::Registry registry;
  registry.add(make_test_quest());
  FlagStore flags;
  quest::Runner runner{registry, flags};

  REQUIRE(runner.start("test_quest").has_value()); // stage "start" has sequence 1
  REQUIRE(runner.advance("test_quest", "nonexistent").has_value());
  CHECK(quest::get_stage("test_quest", flags) == 1);
}

TEST_CASE("tick_quests: stage with auto_advance_to waits for its done_condition") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "auto_q";
  q.name = "Auto";
  q.description = "";
  q.stages.push_back({
      .auto_advance_to = "complete",
      .name = "start",
      .objectives =
          {
              {
                  .done_condition = *corundum::gameplay::dialogue::compile("ember_tracks_found >= 1"),
                  .text = "Find the tracks",
              },
          },
      .sequence = 1,
  });
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
  reg.add(std::move(q));

  FlagStore flags;
  quest::start(*reg.find("auto_q"), flags);

  // Objective not met yet — no advance.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("auto_q", flags) == 1);

  // Objective met — advances exactly once.
  flags["ember_tracks_found"] = 1;
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("auto_q", flags) == 2);
  CHECK(quest::lifecycle(*reg.find("auto_q"), flags) == quest::Lifecycle::Completed);

  // Idempotent: later ticks leave a completed quest alone.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("auto_q", flags) == 2);
}

TEST_CASE("tick_quests: stage without auto_advance_to never auto-advances") {
  quest::Registry reg;
  quest::Quest q = make_quest_with_objectives();
  reg.add(std::move(q));

  FlagStore flags;
  flags["quest.obj_quest"] = 1;
  flags["ember_tracks_found"] = 1;

  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("obj_quest", flags) == 1);
}

TEST_CASE("tick_quests: local.<key> objectives resolve against the active zone") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "zoned";
  q.name = "Zoned";
  q.description = "";
  q.stages.push_back({
      .auto_advance_to = "complete",
      .name = "start",
      .objectives =
          {
              {
                  .done_condition = *corundum::gameplay::dialogue::compile("local.tracks_found >= 1"),
                  .text = "Tracks",
              },
          },
      .sequence = 1,
  });
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
  reg.add(std::move(q));

  FlagStore flags;
  quest::start(*reg.find("zoned"), flags);
  flags["zone.ember_marsh.tracks_found"] = 1;

  // No active zone — the local key does not resolve, so the stage stays.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("zoned", flags) == 1);

  // Active zone — the objective holds and the quest advances.
  quest::tick_quests(reg, flags, "ember_marsh");
  CHECK(quest::get_stage("zoned", flags) == 2);
}

TEST_CASE("tick_quests: an unvalidated self-targeting stage does not loop") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "self";
  q.name = "Self";
  q.description = "";
  q.stages.push_back({
      .auto_advance_to = "start",
      .name = "start",
      .objectives = {{.done_condition = *corundum::gameplay::dialogue::compile("ready == 1"), .text = "A"}},
      .sequence = 1,
  });
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
  reg.add(std::move(q));

  FlagStore flags;
  quest::start(*reg.find("self"), flags);
  flags["ready"] = 1;

  // A self-target is skipped before advance(), so the stage and its breadcrumb
  // count stay put instead of churning every tick.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("self", flags) == 1);
  CHECK(corundum::world::visit_count(flags, "quest.self.seen.start") == 1);
}

TEST_CASE("tick_quests: advances only when every conditioned objective holds, in either order") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "two_obj";
  q.name = "Two";
  q.description = "";
  q.stages.push_back({
      .auto_advance_to = "complete",
      .name = "start",
      .objectives =
          {
              {.done_condition = *corundum::gameplay::dialogue::compile("first_done == 1"), .text = "A"},
              {.done_condition = *corundum::gameplay::dialogue::compile("second_done == 1"), .text = "B"},
          },
      .sequence = 1,
  });
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 2});
  reg.add(std::move(q));

  FlagStore flags;
  quest::start(*reg.find("two_obj"), flags);

  // One of two held — no advance.
  flags["second_done"] = 1;
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("two_obj", flags) == 1);

  // Both held (in the other order) — advances.
  flags["first_done"] = 1;
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("two_obj", flags) == 2);
}

TEST_CASE("tick_quests: a chained auto-advance resolves one stage per tick, idempotently") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "chain";
  q.name = "Chain";
  q.description = "";
  q.stages.push_back({
      .auto_advance_to = "middle",
      .name = "start",
      .objectives = {{.done_condition = *corundum::gameplay::dialogue::compile("ready == 1"), .text = "A"}},
      .sequence = 1,
  });
  q.stages.push_back({
      .auto_advance_to = "complete",
      .name = "middle",
      .objectives = {{.done_condition = *corundum::gameplay::dialogue::compile("ready == 1"), .text = "B"}},
      .sequence = 2,
  });
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 3});
  reg.add(std::move(q));

  FlagStore flags;
  quest::start(*reg.find("chain"), flags);
  flags["ready"] = 1;

  // One tick advances at most one stage (each quest is visited once per call).
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("chain", flags) == 2);

  // The next tick picks up the now-current stage and advances again.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("chain", flags) == 3);
  CHECK(quest::lifecycle(*reg.find("chain"), flags) == quest::Lifecycle::Completed);

  // Resolved quests are inert — later ticks never move them.
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("chain", flags) == 3);
}

TEST_CASE("validate: unknown auto_advance_to target is an error") {
  auto q = make_test_quest();
  q.stages[0].auto_advance_to = "no_such_stage";
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("auto_advance_to") != std::string::npos);
}

TEST_CASE("validate: known auto_advance_to target passes") {
  auto q = make_test_quest();
  q.stages[0].auto_advance_to = "middle";
  CHECK(quest::validate(q).errors.empty());
}

TEST_CASE("validate: auto_advance_to targeting the same stage is an error") {
  auto q = make_test_quest();
  q.stages[0].auto_advance_to = "start";
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("test_quest") != std::string::npos);
  CHECK(errors.front().find("targets itself") != std::string::npos);
  CHECK(errors.front().find("\"start\"") != std::string::npos);
}

TEST_CASE("validate: a self-target is reported once, not also as a cycle") {
  auto q = make_test_quest();
  q.stages[0].auto_advance_to = "start";
  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE(errors.size() == 1);
  CHECK(errors.front().find("targets itself") != std::string::npos);
}

TEST_CASE("validate: an auto_advance_to cycle is an error") {
  quest::Quest q;
  q.quest_id = "cycle";
  q.name = "Cycle";
  q.stages.push_back({.auto_advance_to = "b", .name = "a", .sequence = 1});
  q.stages.push_back({.auto_advance_to = "a", .name = "b", .sequence = 2});
  q.stages.push_back({.name = "complete", .resolved = true, .sequence = 3});

  const std::vector<std::string> errors = quest::validate(q).errors;
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("cycle") != std::string::npos);
}

TEST_CASE("tick_quests: keystone quests are inert (no auto_advance_to anywhere)") {
  const auto path = std::filesystem::path("../keystone/data/quests/ember_of_greyhollow.json");
  if (!std::filesystem::exists(path)) {
    MESSAGE("keystone checkout not present; skipping");
    return;
  }
  quest::Registry reg;
  const auto n_loaded = reg.load_all(disk(), path.parent_path());
  REQUIRE(n_loaded >= 1);

  const auto *q = reg.find("ember_of_greyhollow");
  REQUIRE(q != nullptr);

  check_quest_stays_put(reg, *q);
}

TEST_CASE("quest serialize round-trips through load_quest") {
  const auto result = quest::load_quest(disk(), "engine/tests/fixtures/find_sword.json");
  REQUIRE(result.has_value());
  const auto &q = *result;

  const auto j = quest::serialize(q);

  corundum::test::MemoryContentSource reloaded_content;
  const std::string reloaded_path = "quests/find_sword_reloaded.json";
  reloaded_content.add(reloaded_path, corundum::core::format_json(j));

  const auto reloaded = quest::load_quest(reloaded_content, reloaded_path);
  REQUIRE(reloaded.has_value());

  check_quests_match(*reloaded, q);
}

TEST_CASE("quest serialize round-trips failed, auto_advance_to, and done_condition") {
  quest::Quest q;
  q.quest_id = "round_trip_all";
  q.name = "Round Trip All";
  q.description = "Every optional stage and objective field populated.";

  q.stages.push_back({
      .advances_to = {"done", "failed"},
      .auto_advance_to = "done",
      .name = "start",
      .objectives =
          {
              {.done_condition = std::nullopt, .text = "Bare"},
              {.done_condition = *corundum::gameplay::dialogue::compile("gold >= 1"), .text = "Conditioned"},
          },
      .sequence = 1,
  });
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  q.stages.push_back({.failed = true, .name = "failed", .resolved = true, .sequence = 3});

  const auto j = quest::serialize(q);
  corundum::test::MemoryContentSource reloaded_content;
  const std::string reloaded_path = "quests/round_trip_all_reloaded.json";
  reloaded_content.add(reloaded_path, corundum::core::format_json(j));

  const auto reloaded = quest::load_quest(reloaded_content, reloaded_path);
  REQUIRE(reloaded.has_value());
  check_quests_match(*reloaded, q);
}

TEST_CASE("quest::Registry: load_all reads quests from an in-memory content source") {
  corundum::test::MemoryContentSource source;
  source.add(
      "quests/a.json",
      R"({"type":"quest","id":"a","name":"A","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})");

  quest::Registry registry;
  CHECK(registry.load_all(source, "quests") == 1);
}
