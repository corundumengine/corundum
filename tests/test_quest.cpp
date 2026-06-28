#include <doctest/doctest.h>

#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/quest/loader.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/system.hpp>

#include <filesystem>
#include <fstream>

namespace quest = corundum::gameplay::quest;
using corundum::gameplay::FlagStore;

// ── Helpers ───────────────────────────────────────────────────────────────────

static quest::Quest make_test_quest() {
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

// ── Loader ────────────────────────────────────────────────────────────────────

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

TEST_CASE("quest loader: invalid JSON returns error") {
  std::string tmp = "tests/fixtures/_test_bad_json.json";
  {
    std::ofstream f(tmp);
    f << "not valid json";
  }
  auto result = quest::load_quest(tmp);
  CHECK_FALSE(result.has_value());
  std::filesystem::remove(tmp);
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
  CHECK(corundum::gameplay::visit_count(flags, "quest.test_quest") == 1);
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

// ── quest_flag_key ────────────────────────────────────────────────────────────

TEST_CASE("quest_flag_key formats correctly") {
  CHECK(quest::quest_flag_key("find_sword") == "quest.find_sword");
  CHECK(quest::quest_flag_key("escort_merchant") == "quest.escort_merchant");
  CHECK(quest::quest_flag_key("") == "quest.");
}
