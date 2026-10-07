// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/engine.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  namespace screens = corundum::gameplay::screens;

  corundum::gameplay::dialogue::Graph make_talk_graph() {
    using namespace corundum::gameplay::dialogue;
    Graph graph;
    graph.graph_id = "session_reset";
    graph.speaker = "NPC";

    Node node;
    node.id = "n0";
    node.type = NodeType::Talk;
    node.text = "Hello";
    node.next_id = "end";
    graph.id_to_index[node.id] = graph.nodes.size();
    graph.nodes.push_back(std::move(node));
    return graph;
  }

  void init_engine(corundum::Engine &engine) {
    adopt_platform(engine, 320, 240);
    const std::filesystem::path fixtures = CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR;
    REQUIRE(engine.initialize(make_world_config(fixtures)).has_value());
  }

} // namespace

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("session reset: clears session state but preserves flags and registries") {
  corundum::Engine engine{};
  init_engine(engine);
  // The conversation holds a non-owning pointer to the graph, so it must outlive the test.
  const corundum::gameplay::dialogue::Graph graph = make_talk_graph();
  corundum::gameplay::Gameplay gameplay{engine};

  // Registries and flags must survive the reset.
  corundum::gameplay::quest::Quest quest;
  quest.quest_id = "keep_quest";
  quest.name = "Keep";
  quest.stages.push_back({.name = "start", .sequence = 1});
  REQUIRE(gameplay.quests.add(std::move(quest)));
  engine.flags["keep.me"] = 7;

  // Session state that must be cleared.
  gameplay.dialogue.emplace(graph, engine.flags);
  gameplay.dialogue_npc.emplace();
  gameplay.pending_dialogue_events.push_back({.name = "stale", .args = {}});
  gameplay.active_container_id = "chest";
  gameplay.active_shop_id = "shop";
  gameplay.inventory_cursor = 3;
  gameplay.inventory_scroll = 2;
  gameplay.inventory_lines.push_back({});
  gameplay.inventory_equipment.push_back({});
  gameplay.journal_screen.cursor = 4;
  gameplay.map_screen.cursor = 5;
  gameplay.loot_screen.cursor = 6;
  gameplay.barter_screen.cursor = 7;
  gameplay.codex_screen.entries.push_back({});
  gameplay.codex_screen.cursor = 8;
  gameplay.confirm = screens::ConfirmState{
      .question = "Sure?",
      .on_yes = [](corundum::gameplay::Gameplay &) {},
      .yes_selected = false,
  };
  gameplay.last_hub_mode = screens::Map;
  engine.scene.ui.push(screens::Inventory);
  engine.scene.ui.push(screens::Loot);
  REQUIRE(engine.scene.ui.size() == 2);

  gameplay.reset_session_state();

  CHECK_FALSE(gameplay.dialogue.has_value());
  CHECK_FALSE(gameplay.dialogue_npc.has_value());
  CHECK(gameplay.pending_dialogue_events.empty());
  CHECK(gameplay.active_container_id.empty());
  CHECK(gameplay.active_shop_id.empty());
  CHECK(gameplay.inventory_cursor == 0);
  CHECK(gameplay.inventory_scroll == 0);
  CHECK(gameplay.inventory_lines.empty());
  CHECK(gameplay.inventory_equipment.empty());
  CHECK(gameplay.journal_screen.cursor == 0);
  CHECK(gameplay.map_screen.cursor == 0);
  CHECK(gameplay.loot_screen.cursor == 0);
  CHECK(gameplay.barter_screen.cursor == 0);
  CHECK(gameplay.codex_screen.entries.empty());
  CHECK(gameplay.codex_screen.cursor == 0);
  CHECK(gameplay.confirm.question.empty());
  CHECK_FALSE(gameplay.confirm.on_yes);
  CHECK(gameplay.last_hub_mode == screens::Inventory);
  CHECK(engine.scene.ui.empty());

  // Flags and registries are content/world state, not session state.
  REQUIRE(engine.flags.contains("keep.me"));
  CHECK(engine.flags.at("keep.me") == 7);
  CHECK(gameplay.quests.find("keep_quest") != nullptr);

  engine.cleanup();
}
