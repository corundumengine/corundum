// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/quest/quest.hpp>
#include <cstddef>
#include <doctest/doctest.h>

#include <corundum/core/json_io.hpp>
#include <corundum/dialogue/compiled_expr.hpp>
#include <corundum/quest/loader.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/runner.hpp>
#include <corundum/quest/serialize.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/world/flags.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <vector>

namespace quest = corundum::quest;
using corundum::world::FlagStore;

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

  quest::Quest make_test_quest() {
    quest::Quest q;
    q.quest_id = "test_quest";
    q.name = "Test Quest";
    q.description = "A test quest.";

    q.stages.push_back({"start", 1, false, false, {}});
    q.stages.push_back({"middle", 2, false, false, {}});
    q.stages.push_back({"complete", 3, true, false, {}});
    q.stages.push_back({"failed", 4, true, true, {}});

    return q;
  }

  quest::Quest make_quest_with_objectives() {
    quest::Quest q;
    q.quest_id = "obj_quest";
    q.name = "Obj Quest";
    q.description = "A quest with journal objectives.";

    q.stages.push_back({
        .name = "start",
        .sequence = 1,
        .objectives =
            {
                {.text = "Bare objective", .done_condition = std::nullopt},
                {.text = "Conditioned objective",
                 .done_condition = *corundum::dialogue::compile("ember_tracks_found >= 1")},
            },
    });
    q.stages.push_back({.name = "complete", .sequence = 2, .resolved = true});

    return q;
  }

} // namespace

// ── Loader ────────────────────────────────────────────────────────────────────

TEST_CASE("quest loader: malformed done_condition is a load error") {
  const std::string tmp = "tests/fixtures/_test_bad_done_condition.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[{"text":"t","done_condition":"gold >="}]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("done_condition invalid") != std::string::npos);
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: valid JSON produces correct Quest struct") {
  const auto result = quest::load_quest("tests/fixtures/find_sword.json");
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

TEST_CASE("quest loader: missing type field fails") {
  std::string tmp = "tests/fixtures/_test_no_type.json";
  {
    std::ofstream f(tmp);
    f << R"({"id":"x","name":"x","description":"","stages":[]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: wrong type field fails") {
  std::string tmp = "tests/fixtures/_test_wrong_type.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"dialogue","id":"x","name":"x","description":"","stages":[]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: missing id fails") {
  std::string tmp = "tests/fixtures/_test_no_id.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","name":"x","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: empty id fails") {
  std::string tmp = "tests/fixtures/_test_empty_id.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"","name":"x","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: duplicate stage sequences rejected") {
  std::string tmp = "tests/fixtures/_test_dup_seq.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[]},
      {"name":"b","sequence":1,"resolved":true,"objectives":[]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: sequence <= 0 rejected") {
  std::string tmp = "tests/fixtures/_test_bad_seq.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":0,"resolved":true,"objectives":[]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: no resolved stage rejected") {
  std::string tmp = "tests/fixtures/_test_no_resolved.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"objectives":[]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: failed flag auto-sets resolved") {
  std::string tmp = "tests/fixtures/_test_failed_flag.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"fail","sequence":1,"failed":true,"objectives":[]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  REQUIRE(result.has_value());
  CHECK(result->stages[0].failed);
  CHECK(result->stages[0].resolved);
  std::filesystem::remove(tmp);
}

TEST_CASE("quest loader: advances_to parses and round-trips") {
  std::string tmp = "tests/fixtures/_test_advances_to.json";
  {
    std::ofstream f(tmp);
    f << R"({"type":"quest","id":"x","name":"x","description":"","stages":[
      {"name":"a","sequence":1,"advances_to":["b","c"],"objectives":[]},
      {"name":"b","sequence":2,"resolved":true,"objectives":[]},
      {"name":"c","sequence":3,"resolved":true,"objectives":[]}
    ]})";
  }
  auto result = quest::load_quest(tmp);
  REQUIRE(result.has_value());
  REQUIRE(result->stages[0].advances_to.size() == 2);
  CHECK(result->stages[0].advances_to[0] == "b");
  CHECK(result->stages[0].advances_to[1] == "c");
  CHECK(result->stages[1].advances_to.empty());

  const auto j = quest::serialize(*result);
  const auto tmp2 = std::filesystem::path("tests/fixtures/tmp_advances_to.json");
  REQUIRE(corundum::core::write_json(tmp2, j).has_value());
  const auto reloaded = quest::load_quest(tmp2.string());
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->stages[0].advances_to.size() == 2);
  CHECK(reloaded->stages[0].advances_to[0] == "b");
  CHECK(reloaded->stages[0].advances_to[1] == "c");

  std::filesystem::remove(tmp);
  std::filesystem::remove(tmp2);
}

TEST_CASE("quest loader: keystone ember_of_greyhollow still loads clean") {
  const auto path = std::filesystem::path("../keystone/data/quests/ember_of_greyhollow.json");
  if (!std::filesystem::exists(path)) {
    MESSAGE("keystone checkout not present; skipping");
    return;
  }
  const auto result = quest::load_quest(path);
  REQUIRE(result.has_value());
  CHECK(result->quest_id == "ember_of_greyhollow");
  CHECK_FALSE(result->stages.empty());
}

TEST_CASE("quest loader: invalid JSON returns error") {
  std::string tmp = "tests/fixtures/_test_bad_json.json";
  {
    std::ofstream f(tmp);
    f << "not valid json";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  CHECK(result.error().find("malformed quest JSON") != std::string::npos);
  CHECK(result.error().find(tmp) != std::string::npos);
  std::filesystem::remove(tmp);
}

// ── validate ──────────────────────────────────────────────────────────────────

TEST_CASE("validate: valid quest returns empty vector") {
  auto q = make_test_quest();
  CHECK(quest::validate(q).empty());
}

TEST_CASE("validate: duplicate stage names") {
  auto q = make_test_quest();
  q.stages.push_back({"start", 5, true, false, {}});
  const auto errors = quest::validate(q);
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("duplicate stage name") != std::string::npos);
}

TEST_CASE("validate: duplicate sequences") {
  auto q = make_test_quest();
  q.stages.push_back({"unique_name", 2, true, false, {}});
  const auto errors = quest::validate(q);
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("duplicate sequence") != std::string::npos);
}

TEST_CASE("validate: no resolved stage") {
  quest::Quest q;
  q.quest_id = "unresolved";
  q.stages.push_back({"a", 1, false, false, {}});
  q.stages.push_back({"b", 2, false, false, {}});
  const auto errors = quest::validate(q);
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("no resolved stage") != std::string::npos);
}

// ── advances_to validation ────────────────────────────────────────────────────

TEST_CASE("validate: unknown advances_to target is an error") {
  auto q = make_test_quest();
  q.stages[0].advances_to.emplace_back("no_such_stage");
  const auto errors = quest::validate(q);
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("advances_to") != std::string::npos);
}

TEST_CASE("validate: known advances_to targets pass") {
  auto q = make_test_quest();
  q.stages[0].advances_to = {"middle"};
  CHECK(quest::validate(q).empty());
}

TEST_CASE("validate: out-of-order sequences surface as a warning, not an error") {
  quest::Quest q;
  q.quest_id = "desync";
  q.name = "Desync";
  q.description = "";
  q.stages.push_back({"start", 1, false, false, {}});
  q.stages.push_back({"complete", 3, true, false, {}});
  q.stages.push_back({"middle", 2, false, false, {}});

  std::vector<std::string> warnings;
  const auto errors = quest::validate(q, &warnings);
  CHECK(errors.empty());
  REQUIRE_FALSE(warnings.empty());
  CHECK(warnings.front().find("sequence") != std::string::npos);
}

TEST_CASE("validate: in-order quest produces no warnings") {
  auto q = make_test_quest();
  std::vector<std::string> warnings;
  CHECK(quest::validate(q, &warnings).empty());
  CHECK(warnings.empty());
}

// ── find_stage ────────────────────────────────────────────────────────────────

TEST_CASE("Quest::find_stage returns correct stage for known name") {
  auto q = make_test_quest();
  const auto *s = q.find_stage("middle");
  REQUIRE(s != nullptr);
  CHECK(s->name == "middle");
  CHECK(s->sequence == 2);
}

TEST_CASE("Quest::find_stage returns nullptr for unknown name") {
  auto q = make_test_quest();
  CHECK(q.find_stage("does_not_exist") == nullptr);
}

// ── start ─────────────────────────────────────────────────────────────────────

TEST_CASE("start sets quest.{id} to first stage sequence") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  CHECK(corundum::world::visit_count(flags, "quest.test_quest") == 1);
}

TEST_CASE("start is no-op when already started") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 2;

  quest::start(q, flags);
  CHECK(flags["quest.test_quest"] == 2);
}

// ── advance ───────────────────────────────────────────────────────────────────

TEST_CASE("advance moves to correct stage sequence") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 1;

  quest::advance(q, "middle", flags);
  CHECK(flags["quest.test_quest"] == 2);
}

TEST_CASE("advance works on unstarted quest") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  CHECK(flags["quest.test_quest"] == 2);
}

TEST_CASE("advance is no-op for unknown stage name") {
  auto q = make_test_quest();
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
  q.stages.push_back({"a", 1, false, false, {}});
  q.stages.push_back({"b", 2, false, false, {}});
  q.stages.push_back({"c", 3, true, false, {}});
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
  q.stages.push_back({"a", 1, false, false, {}});
  q.stages.push_back({"b", 2, false, false, {}});
  q.stages.push_back({"c", 3, true, false, {}});
  q.stages[0].advances_to = {"b"};
  q.stages[0].auto_advance_to = "c";

  FlagStore flags;
  flags["quest.auto_q"] = 1;

  // "c" is the stage's auto_advance_to — legal even though not in advances_to.
  quest::advance(q, "c", flags);
  CHECK(flags["quest.auto_q"] == 3);
}

// ── get_stage ─────────────────────────────────────────────────────────────────

TEST_CASE("get_stage reads flag correctly") {
  FlagStore flags;
  flags["quest.test_quest"] = 3;

  CHECK(quest::get_stage("test_quest", flags) == 3);
}

TEST_CASE("get_stage returns 0 when key absent") {
  FlagStore flags;
  CHECK(quest::get_stage("test_quest", flags) == 0);
}

// ── is_complete ───────────────────────────────────────────────────────────────

TEST_CASE("is_complete true when flag matches any resolved stage") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3; // "complete" is resolved

  CHECK(quest::is_complete(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("is_complete false when inactive or on non-resolved stage") {
  auto q = make_test_quest();
  FlagStore flags;

  // Inactive
  CHECK_FALSE(quest::is_complete(q, flags));

  // Non-resolved stage
  flags["quest.test_quest"] = 1;
  CHECK_FALSE(quest::is_complete(q, flags));
}

TEST_CASE("multiple resolved stages both satisfy is_complete") {
  auto q = make_test_quest();
  FlagStore flags;

  flags["quest.test_quest"] = 3;
  CHECK(quest::is_complete(q, flags));

  flags["quest.test_quest"] = 4;
  CHECK(quest::is_complete(q, flags));
}

// ── is_failed ─────────────────────────────────────────────────────────────────

TEST_CASE("is_failed true when flag matches failed stage") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 4; // "failed" is failed+resolved

  CHECK(quest::is_complete(q, flags));
  CHECK(quest::is_failed(q, flags));
}

TEST_CASE("is_failed false for non-failed resolved stage") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3; // "complete" is resolved but not failed

  CHECK(quest::is_complete(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("is_failed false when not started") {
  auto q = make_test_quest();
  FlagStore flags;

  CHECK_FALSE(quest::is_failed(q, flags));
}

// ── active_quests ─────────────────────────────────────────────────────────────

TEST_CASE("active_quests returns only quests with stage > 0") {
  const auto tmp_dir = std::filesystem::temp_directory_path() / "quest_test_active";
  std::filesystem::create_directories(tmp_dir);
  {
    std::ofstream f(tmp_dir / "q1.json");
    f << R"({"type":"quest","id":"q1","name":"Q1","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}
    ]})";
  }
  {
    std::ofstream f(tmp_dir / "q2.json");
    f << R"({"type":"quest","id":"q2","name":"Q2","description":"","stages":[
      {"name":"a","sequence":1,"resolved":true,"objectives":[]}
    ]})";
  }

  quest::Registry reg;
  const auto n_loaded = reg.load_all(tmp_dir);
  CHECK(n_loaded == 2);
  CHECK(reg.size() == 2);

  FlagStore flags;
  flags["quest.q1"] = 1; // q1 active
  // q2 not started

  auto active = quest::active_quests(reg, flags);
  REQUIRE(active.size() == 1);
  CHECK(active[0]->quest_id == "q1");

  std::filesystem::remove_all(tmp_dir);
}

// ── Registry ──────────────────────────────────────────────────────────────────

TEST_CASE("registry load_all count matches size with duplicate ids") {
  const auto tmp_dir = std::filesystem::temp_directory_path() / "quest_test_dup_count";
  std::filesystem::create_directories(tmp_dir);
  {
    std::ofstream f(tmp_dir / "a.json");
    f << R"({"type":"quest","id":"dup","name":"A","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})";
  }
  {
    std::ofstream f(tmp_dir / "b.json");
    f << R"({"type":"quest","id":"dup","name":"B","description":"","stages":[{"name":"s","sequence":1,"resolved":true,"objectives":[]}]})";
  }

  quest::Registry reg;
  int loaded = reg.load_all(tmp_dir);
  CHECK(loaded == 1);
  CHECK(reg.size() == 1);
  std::filesystem::remove_all(tmp_dir);
}

// ── Registry ──────────────────────────────────────────────────────────────────

TEST_CASE("registry load_all loads valid files, skips bad ones") {
  // Create temp dir with mixed files
  const auto tmp_dir = std::filesystem::temp_directory_path() / "quest_test_registry";
  std::filesystem::create_directories(tmp_dir);
  std::filesystem::copy_file("tests/fixtures/find_sword.json", tmp_dir / "find_sword.json",
                             std::filesystem::copy_options::overwrite_existing);

  // Bad file
  {
    std::ofstream f(tmp_dir / "bad.json");
    f << R"({"type":"quest"})"; // missing required fields
  }

  // Non-JSON file
  {
    std::ofstream f(tmp_dir / "notes.txt");
    f << "not a quest";
  }

  quest::Registry reg;
  int loaded = reg.load_all(tmp_dir);

  CHECK(loaded == 1);
  CHECK(reg.size() == 1);

  const auto *q = reg.find("find_sword");
  REQUIRE(q != nullptr);
  CHECK(q->quest_id == "find_sword");

  CHECK(reg.find("does_not_exist") == nullptr);

  std::filesystem::remove_all(tmp_dir);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TEST_CASE("lifecycle: helped path") {
  auto q = make_test_quest();
  FlagStore flags;

  // Not started
  CHECK(quest::get_stage("test_quest", flags) == 0);

  // Start
  quest::start(q, flags);
  CHECK(quest::get_stage("test_quest", flags) == 1);
  CHECK_FALSE(quest::is_complete(q, flags));

  // Advance to middle
  quest::advance(q, "middle", flags);
  CHECK(quest::get_stage("test_quest", flags) == 2);
  CHECK_FALSE(quest::is_complete(q, flags));

  // Advance to resolved
  quest::advance(q, "complete", flags);
  CHECK(quest::get_stage("test_quest", flags) == 3);
  CHECK(quest::is_complete(q, flags));
  CHECK_FALSE(quest::is_failed(q, flags));
}

TEST_CASE("lifecycle: betrayed path") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  quest::advance(q, "failed", flags);
  CHECK(quest::get_stage("test_quest", flags) == 4);
  CHECK(quest::is_complete(q, flags));
  CHECK(quest::is_failed(q, flags));
}

TEST_CASE("lifecycle: organic discovery") {
  auto q = make_test_quest();
  FlagStore flags;

  // Advance without start — works via organic discovery
  quest::advance(q, "middle", flags);
  CHECK(quest::get_stage("test_quest", flags) == 2);
}

// ── Status façade ─────────────────────────────────────────────────────────────

TEST_CASE("lifecycle: all four states map to the right enum") {
  auto q = make_test_quest();
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
  auto q = make_test_quest();
  FlagStore flags;
  CHECK(quest::current_stage(q, flags) == nullptr);
}

TEST_CASE("current_stage matches the active flag") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 2;
  const auto *stage = quest::current_stage(q, flags);
  REQUIRE(stage != nullptr);
  CHECK(stage->name == "middle");
}

TEST_CASE("current_stage points at the resolved ending stage") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 3;
  const auto *stage = quest::current_stage(q, flags);
  REQUIRE(stage != nullptr);
  CHECK(stage->name == "complete");
}

TEST_CASE("current_stage returns nullptr for a sequence with no stage") {
  auto q = make_test_quest();
  FlagStore flags;
  flags["quest.test_quest"] = 99;
  CHECK(quest::current_stage(q, flags) == nullptr);
}

TEST_CASE("objectives returns nothing for an unstarted quest") {
  auto q = make_quest_with_objectives();
  FlagStore flags;
  CHECK(quest::objectives(q, flags).empty());
}

TEST_CASE("objectives reports bare and conditioned objectives") {
  auto q = make_quest_with_objectives();
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
  auto q = make_quest_with_objectives();
  FlagStore flags;
  flags["quest.obj_quest"] = 1;
  flags["ember_tracks_found"] = 1;

  const auto views = quest::objectives(q, flags);
  REQUIRE(views.size() == 2);
  CHECK_FALSE(views[0].done);
  CHECK(views[1].done);
}

// ── Breadcrumbs ───────────────────────────────────────────────────────────────

TEST_CASE("start records the quest.<id>.seen.<stage> breadcrumb") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::start(q, flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.start"));
}

TEST_CASE("advance records a breadcrumb for each stage entered") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.middle"));

  quest::advance(q, "complete", flags);
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.complete"));
  // Breadcrumb from an earlier stage remains.
  CHECK(corundum::world::has_flag(flags, "quest.test_quest.seen.middle"));
}

TEST_CASE("advance to an unknown stage records no breadcrumb") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "does_not_exist", flags);
  CHECK_FALSE(corundum::world::has_flag(flags, "quest.test_quest.seen.does_not_exist"));
}

TEST_CASE("advance to a stage already entered re-records the breadcrumb") {
  auto q = make_test_quest();
  FlagStore flags;

  quest::advance(q, "middle", flags);
  quest::advance(q, "middle", flags);
  CHECK(corundum::world::visit_count(flags, "quest.test_quest.seen.middle") == 2);
}

// ── quest_flag_key ────────────────────────────────────────────────────────────

TEST_CASE("quest_flag_key formats correctly") {
  CHECK(quest::quest_flag_key("find_sword") == "quest.find_sword");
  CHECK(quest::quest_flag_key("escort_merchant") == "quest.escort_merchant");
  CHECK(quest::quest_flag_key("") == "quest.");
}

// ── Runner ────────────────────────────────────────────────────────────────────

TEST_CASE("Runner: start on an unknown id returns an error and sets no flag") {
  quest::Registry registry;
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
  quest::Registry registry;
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

// ── auto_advance_to ───────────────────────────────────────────────────────────

TEST_CASE("tick_quests: stage with auto_advance_to waits for its done_condition") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "auto_q";
  q.name = "Auto";
  q.description = "";
  q.stages.push_back({.name = "start",
                      .sequence = 1,
                      .objectives = {{.text = "Find the tracks",
                                      .done_condition = *corundum::dialogue::compile("ember_tracks_found >= 1")}},
                      .auto_advance_to = "complete"});
  q.stages.push_back({.name = "complete", .sequence = 2, .resolved = true});
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

TEST_CASE("tick_quests: advances only when every conditioned objective holds, in either order") {
  quest::Registry reg;
  quest::Quest q;
  q.quest_id = "two_obj";
  q.name = "Two";
  q.description = "";
  q.stages.push_back({.name = "start",
                      .sequence = 1,
                      .objectives =
                          {
                              {.text = "A", .done_condition = *corundum::dialogue::compile("first_done == 1")},
                              {.text = "B", .done_condition = *corundum::dialogue::compile("second_done == 1")},
                          },
                      .auto_advance_to = "complete"});
  q.stages.push_back({.name = "complete", .sequence = 2, .resolved = true});
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
  q.stages.push_back({.name = "start",
                      .sequence = 1,
                      .objectives = {{.text = "A", .done_condition = *corundum::dialogue::compile("ready == 1")}},
                      .auto_advance_to = "middle"});
  q.stages.push_back({.name = "middle",
                      .sequence = 2,
                      .objectives = {{.text = "B", .done_condition = *corundum::dialogue::compile("ready == 1")}},
                      .auto_advance_to = "complete"});
  q.stages.push_back({.name = "complete", .sequence = 3, .resolved = true});
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
  const auto errors = quest::validate(q);
  REQUIRE_FALSE(errors.empty());
  CHECK(errors.front().find("auto_advance_to") != std::string::npos);
}

TEST_CASE("validate: known auto_advance_to target passes") {
  auto q = make_test_quest();
  q.stages[0].auto_advance_to = "middle";
  CHECK(quest::validate(q).empty());
}

TEST_CASE("tick_quests: keystone quests are inert (no auto_advance_to anywhere)") {
  const auto path = std::filesystem::path("../keystone/data/quests/ember_of_greyhollow.json");
  if (!std::filesystem::exists(path)) {
    MESSAGE("keystone checkout not present; skipping");
    return;
  }
  quest::Registry reg;
  const auto n_loaded = reg.load_all(path.parent_path());
  REQUIRE(n_loaded >= 1);

  const auto *q = reg.find("ember_of_greyhollow");
  REQUIRE(q != nullptr);
  for (const auto &stage : q->stages)
    CHECK_FALSE(stage.auto_advance_to.has_value());

  FlagStore flags;
  flags["quest.ember_of_greyhollow"] = q->stages[0].sequence;
  quest::tick_quests(reg, flags, {});
  CHECK(quest::get_stage("ember_of_greyhollow", flags) == q->stages[0].sequence);
}

// ── Round-trip ────────────────────────────────────────────────────────────────

TEST_CASE("quest serialize round-trips through load_quest") {
  const auto result = quest::load_quest("tests/fixtures/find_sword.json");
  REQUIRE(result.has_value());
  const auto &q = *result;

  const auto j = quest::serialize(q);

  const auto tmp = std::filesystem::path("tests/fixtures/tmp_find_sword.json");
  auto write_result = corundum::core::write_json(tmp, j);
  REQUIRE(write_result.has_value());

  const auto reloaded = quest::load_quest(tmp.string());
  REQUIRE(reloaded.has_value());
  const auto &q2 = *reloaded;

  CHECK(q2.quest_id == q.quest_id);
  CHECK(q2.name == q.name);
  CHECK(q2.description == q.description);
  REQUIRE(q2.stages.size() == q.stages.size());

  for (std::size_t i = 0; i < q.stages.size(); ++i) {
    CHECK(q2.stages[i].name == q.stages[i].name);
    CHECK(q2.stages[i].sequence == q.stages[i].sequence);
    CHECK(q2.stages[i].resolved == q.stages[i].resolved);
    CHECK(q2.stages[i].failed == q.stages[i].failed);
    CHECK(q2.stages[i].advances_to == q.stages[i].advances_to);
    REQUIRE(q2.stages[i].objectives.size() == q.stages[i].objectives.size());
    for (std::size_t j = 0; j < q.stages[i].objectives.size(); ++j) {
      CHECK(q2.stages[i].objectives[j].text == q.stages[i].objectives[j].text);
    }
  }

  std::filesystem::remove(tmp);
}
