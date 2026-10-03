// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/compiled_expr.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/dialog_layout.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>
#include <cstddef>
#include <doctest/doctest.h>

#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>

#include "ui/recording_renderer.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using corundum::platform::DrawRect;
using corundum::platform::DrawSprite;
using corundum::platform::DrawText;
using corundum::test::make_border;
using corundum::test::RecordingRenderer;

// ── dialog_box_update ─────────────────────────────────────────────────────────

namespace {

  // Builds a Talk graph with the requested graph_id, speaker, and a node literally
  // named "n0". Used to reproduce the Keystone bug where two NPCs share a first-node
  // id but have different speakers.
  corundum::gameplay::dialogue::Graph make_talk_graph(std::string graph_id, std::string speaker,
                                                      std::string talk_text) {
    using namespace corundum::gameplay::dialogue;
    Graph g;
    g.graph_id = std::move(graph_id);
    g.speaker = std::move(speaker);
    Node n;
    n.id = "n0";
    n.type = NodeType::Talk;
    n.text = std::move(talk_text);
    n.next_id = "end";
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

  // Builds a Choice graph where the second option is gated by quest_is_at.
  // Used to verify that threading the quest registry through dialog_box_update
  // yields the gated choice in the layout (not hidden by a parse failure).
  corundum::gameplay::dialogue::Graph make_choice_graph_with_quest_gate() {
    using namespace corundum::gameplay::dialogue;
    Graph g;
    g.graph_id = "gated";
    g.speaker = "Gatekeeper";
    Node n;
    n.id = "n0";
    n.type = NodeType::Choice;
    n.choices = {
        {.label = "Always.", .target_id = "a"},
        {.label = "Secret.", .target_id = "b", .condition = *compile("quest_is_at(ember, done)")},
    };
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

  // Builds a Choice graph whose second option is gated by a plain boolean flag,
  // for exercising a visibility change at an otherwise unchanged node.
  corundum::gameplay::dialogue::Graph make_flag_gated_choice_graph() {
    using namespace corundum::gameplay::dialogue;
    Graph g;
    g.graph_id = "flag_gated";
    g.speaker = "Gatekeeper";
    Node n;
    n.id = "n0";
    n.type = NodeType::Choice;
    n.choices = {
        {.label = "Always.", .target_id = "a"},
        {.label = "Secret.", .target_id = "b", .condition = *compile("secret == true")},
    };
    g.id_to_index[n.id] = 0;
    g.nodes.push_back(std::move(n));
    return g;
  }

} // namespace

TEST_CASE("dialog_box_update: switching graphs with a shared first-node id rebuilds the layout") {
  // Reproduces the Keystone cancel + retalk bug: every dialogue file starts at a
  // node named "n0". After cancelling the innkeeper and starting the villager, the
  // stale-check must see that the graph id changed and rebuild — otherwise the
  // innkeeper's speaker/text stays on screen.
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();

  const auto innkeeper = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  const auto villager = make_talk_graph("villager_generic", "Villager", "Did you see the harvest moon last night?");

  corundum::world::FlagStore flags;
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  const corundum::gameplay::dialogue::Conversation innkeeper_conversation{innkeeper, flags};
  corundum::gameplay::screens::dialog_box_update(ds, innkeeper_conversation, r, viewport, skin, 0.f);
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->speaker == "Innkeeper");
  CHECK_FALSE(ds.layout->body_lines.empty());
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Cancel and switch NPCs.
  const corundum::gameplay::dialogue::Conversation villager_conversation{villager, flags};
  corundum::gameplay::screens::dialog_box_update(ds, villager_conversation, r, viewport, skin, 0.f);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->speaker == "Villager");
  REQUIRE_FALSE(ds.layout->body_lines.empty());
  CHECK(ds.layout->body_lines.front() == "Did you see the harvest moon last night?");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: an ended conversation hides the box") {
  // Regression: the render system only calls dialog_box_update while scene.dialogue is
  // engaged, and update_dialogue() resets the optional the frame the conversation
  // ends. The box must be hidden by that frame — never repainted from a stale layout.
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();

  const auto graph = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  corundum::world::FlagStore flags;
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::gameplay::dialogue::Conversation conversation{graph, flags};
  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, viewport, skin, 0.f);
  REQUIRE(ds.visible);
  REQUIRE(ds.layout.has_value());

  // The Talk node's next is "end", so Select closes the conversation.
  corundum::input::InputIntent select{};
  select.select = true;
  static_cast<void>(conversation.update(select));
  CHECK_FALSE(conversation.is_active());

  // The frame after the conversation ends, the box must be hidden, not stale-visible.
  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, viewport, skin, 0.f);
  CHECK_FALSE(ds.visible);
}

TEST_CASE("dialog_box_update: quest-gated choice is drawn when the registry is threaded") {
  // Verifies the render-side end of Bug 1: a Choice node with a quest_is_at gate
  // is rendered with the gated option present when dialog_box_update receives the
  // registry and the matching quest.<id> flag is set. Before the fix, the parse
  // would error on a null registry and the choice would be hidden.
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();

  corundum::gameplay::quest::Registry quests;
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({.name = "start", .sequence = 1});
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  quests.add(std::move(q));

  const auto graph = make_choice_graph_with_quest_gate();

  corundum::world::FlagStore flags;
  flags["quest.ember"] = 2; // matches stage "done" (sequence 2)
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags, &quests};

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, viewport, skin, 0.f);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 2);
  REQUIRE(ds.layout->choices[0].lines.size() == 1);
  REQUIRE(ds.layout->choices[1].lines.size() == 1);
  CHECK(ds.layout->choices[0].lines.front() == "Always.");
  CHECK(ds.layout->choices[1].lines.front() == "Secret.");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: a vertical-only viewport change rebuilds the panel geometry") {
  // Regression: the stale check tracked only panel width, so growing the window
  // vertically left the box pinned at its old height and offset (build_layout derives
  // panel_h and panel_y from viewport.y).
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();

  const auto graph = make_talk_graph("innkeeper_intro", "Innkeeper", "Welcome, traveller.");
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, {.x = 1280.f, .y = 720.f}, skin, 0.f);
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  const float first_h = ds.layout->panel_size.y;
  const float first_y = ds.layout->panel_pos.y;
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Same width, taller viewport.
  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, {.x = 1280.f, .y = 900.f}, skin, 0.f);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  CHECK(ds.layout->panel_size.y > first_h);
  CHECK(ds.layout->panel_pos.y != first_y);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("dialog_box_update: a visibility change at the same node rebuilds the layout") {
  // Regression: the cache key was (graph, node, viewport). A Choice node re-visited with
  // different flags (quest progress between visits, or a goto_graph loop) kept the old
  // condition-evaluated choice list.
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();

  const auto graph = make_flag_gated_choice_graph();
  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, viewport, skin, 0.f);
  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 1);
  CHECK(ds.layout->choices[0].lines.front() == "Always.");
  // NOLINTEND(bugprone-unchecked-optional-access)

  // Quest progress unlocks the gated option while the node stays the same.
  flags["secret"] = 1;
  corundum::gameplay::screens::dialog_box_update(ds, conversation, r, viewport, skin, 0.f);

  REQUIRE(ds.layout.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above aborts the case when empty.
  REQUIRE(ds.layout->choices.size() == 2);
  CHECK(ds.layout->choices[1].lines.front() == "Secret.");
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("build_layout: a choice label wider than the panel keeps every wrapped line") {
  // Regression: the layout kept only the first wrapped line of a choice label, silently
  // truncating any option too long for the panel.
  RecordingRenderer r;
  using namespace corundum::gameplay::dialogue;

  Graph graph;
  graph.graph_id = "wrapped";
  Node node;
  node.id = "n0";
  node.type = NodeType::Choice;
  node.choices = {{.label = "Alpha Beta Gamma Delta", .target_id = "a"}};
  graph.id_to_index[node.id] = 0;
  graph.nodes.push_back(std::move(node));

  corundum::world::FlagStore flags;
  const corundum::gameplay::dialogue::Conversation conversation{graph, flags};

  // Narrow viewport: choice_w = 200 - 2*20 (margin) - 2*20 (inset) - 16 (cursor) = 104px,
  // i.e. 13 glyphs at the recorder's 8px each.
  corundum::ui::PanelStyle style{};
  style.margin = 20.f;
  style.panel_height_frac = 0.32f;
  const corundum::gameplay::screens::DialogLayout layout =
      corundum::gameplay::screens::build_layout(conversation, style, 4, {.x = 200.f, .y = 720.f},
                                                [&](std::string_view text) { return r.measure_text(0, text, 22); });

  REQUIRE(layout.choices.size() == 1);
  REQUIRE(layout.choices[0].lines.size() > 1);

  // Re-joining the wrapped lines recovers every word of the label.
  std::string joined;
  for (const std::string &line : layout.choices[0].lines) {
    if (!joined.empty())
      joined += ' ';
    joined += line;
  }
  CHECK(joined == "Alpha Beta Gamma Delta");
}

TEST_CASE("dialog_box_render: wrapped continuation lines keep the selected colour and hanging indent") {
  // A selected choice whose label wrapped: only the first line shows the "> " cursor, but
  // every line stays highlighted (style.selected) and aligned in the label column.
  RecordingRenderer r;
  corundum::gameplay::screens::DialogBoxState ds{};
  corundum::ui::PanelSkin skin{};
  skin.border = make_border();
  ds.visible = true;

  corundum::gameplay::screens::DialogLayout layout{};
  layout.panel_pos = {.x = 0.f, .y = 0.f};
  layout.panel_size = {.x = 400.f, .y = 200.f};
  layout.inset = 10.f;
  layout.node_type = corundum::gameplay::dialogue::NodeType::Choice;
  layout.choices = {
      corundum::gameplay::screens::ChoiceLayout{.index = 0, .lines = {"first line", "second line"}},
      corundum::gameplay::screens::ChoiceLayout{.index = 1, .lines = {"other"}},
  };
  ds.layout = std::move(layout);

  corundum::gameplay::screens::dialog_box_render(ds, r, skin);

  // chrome (1 rect + 8 sprites) + "Choose:" header + choice 0 (2 lines × 2) + choice 1 (1 × 2).
  REQUIRE(r.log.size() == 9 + 1 + 4 + 2);
  const float advance = corundum::ui::cursor_advance(r, skin.style);

  // First line: "> " cursor in the selected colour, label in the cursor column.
  CHECK(std::get<DrawText>(r.log[10]).text == "> ");
  CHECK(std::get<DrawText>(r.log[11]).colour.r == skin.style.selected.r);
  CHECK(std::get<DrawText>(r.log[11]).position.x == 10.f + advance); // px + inset + advance

  // Second line: no cursor, same colour, same hanging indent.
  CHECK(std::get<DrawText>(r.log[12]).text == "  ");
  CHECK(std::get<DrawText>(r.log[13]).colour.r == skin.style.selected.r);
  CHECK(std::get<DrawText>(r.log[13]).position.x == std::get<DrawText>(r.log[11]).position.x);
}

// ── inventory_panel_render ───────────────────────────────────────────────────

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_render: 2 rows emit chrome, header, and one option pair per row") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.count = 2, .name = "Apple"},
      {.count = 1, .name = "Salt"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);

  // panel_chrome: 1 DrawRect + 8 DrawSprite; then the "Inventory" header DrawText;
  // then 2 rows × 2 DrawText (cursor + label).
  REQUIRE(r.log.size() == 9 + 1 + 4);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));

  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  CHECK(header.colour.r == style.speaker.r);

  const DrawText &row0_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &row0_label = std::get<DrawText>(r.log[11]);
  const DrawText &row1_cursor = std::get<DrawText>(r.log[12]);
  const DrawText &row1_label = std::get<DrawText>(r.log[13]);

  CHECK(row0_cursor.text == "> ");
  CHECK(row0_label.text == "Apple  x2");
  CHECK(row0_cursor.colour.r == style.selected.r); // cursor row uses style.selected
  CHECK(row0_label.colour.r == style.selected.r);

  CHECK(row1_cursor.text == "  ");
  CHECK(row1_label.text == "Salt  x1");
  CHECK(row1_cursor.colour.r == style.choice.r); // non-cursor row uses style.choice
  CHECK(row1_label.colour.r == style.choice.r);
  CHECK(row1_label.position.y > row0_label.position.y); // rows stack downward
}

TEST_CASE("inventory_panel_render: empty list renders header plus one (empty) line") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, {}, 0, viewport);

  // Chrome (9) + header + one "(empty)" line.
  REQUIRE(r.log.size() == 9 + 1 + 1);
  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  const DrawText &empty = std::get<DrawText>(r.log[10]);
  CHECK(empty.text == "(empty)");
}

TEST_CASE("inventory_panel_render: cursor is clamped into the row range") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.count = 1, .name = "A"},
      {.count = 1, .name = "B"},
      {.count = 1, .name = "C"},
  };
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  // cursor 99 → clamps to the last row.
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 99, viewport);
  const DrawText &last_cursor = std::get<DrawText>(r.log[r.log.size() - 2]);
  CHECK(last_cursor.text == "> ");
  const DrawText &last_label = std::get<DrawText>(r.log.back());
  CHECK(last_label.text == "C  x1");

  // cursor -5 → clamps to the first row.
  RecordingRenderer r2;
  corundum::gameplay::screens::inventory_panel_render(r2, style, border, lines, -5, viewport);
  const DrawText &first_cursor = std::get<DrawText>(r2.log[10]);
  CHECK(first_cursor.text == "> ");
  const DrawText &first_label = std::get<DrawText>(r2.log[11]);
  CHECK(first_label.text == "A  x1");
}

// ── build_inventory_lines ────────────────────────────────────────────────────

// doctest's CHECK macros expand to control flow, so the assertion count — not the test's logic —
// dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_render: the highlighted row's description draws as a footer tooltip") {
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};
  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.count = 1, .name = "Salt", .description = "A pinch of river salt."},
      {.count = 1, .name = "Hammer", .description = "Osric's heirloom."},
  };
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  // Cursor 0 draws the first row's description, not the second's.
  RecordingRenderer r;
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);
  bool first_description = false;
  bool second_description = false;
  for (const auto &call : r.log) {
    if (const auto *text = std::get_if<DrawText>(&call); text != nullptr) {
      first_description = first_description || text->text == "A pinch of river salt.";
      second_description = second_description || text->text == "Osric's heirloom.";
    }
  }
  CHECK(first_description);
  CHECK_FALSE(second_description);

  // Moving the cursor to the second row swaps the tooltip.
  RecordingRenderer r2;
  corundum::gameplay::screens::inventory_panel_render(r2, style, border, lines, 1, viewport);
  bool second_now = false;
  bool first_now = false;
  for (const auto &call : r2.log) {
    if (const auto *text = std::get_if<DrawText>(&call); text != nullptr) {
      second_now = second_now || text->text == "Osric's heirloom.";
      first_now = first_now || text->text == "A pinch of river salt.";
    }
  }
  CHECK(second_now);
  CHECK_FALSE(first_now);
}

TEST_CASE("build_inventory_lines: skips zero counts and non-item flags, sorts by name, falls back to id") {
  corundum::world::FlagStore flags;
  flags["item.a"] = 2;
  flags["item.b"] = 0;  // zero count → dropped
  flags["item.c"] = 1;  // unknown to the registry → id fallback
  flags["quest.x"] = 3; // non-item key → ignored

  corundum::gameplay::item::Registry items;
  corundum::gameplay::item::Item a;
  a.id = "a";
  a.name = "Apple";
  a.description = "Crisp and red.";
  items.add(std::move(a));

  const auto lines = corundum::gameplay::screens::build_inventory_lines(flags, items);

  REQUIRE(lines.size() == 2);
  CHECK(lines[0].name == "Apple");
  CHECK(lines[0].count == 2);
  CHECK(lines[0].description == "Crisp and red.");
  CHECK(lines[1].name == "c");
  CHECK(lines[1].count == 1);
  CHECK(lines[1].description.empty()); // no definition → no tooltip
}

TEST_CASE("build_inventory_lines: groups items by category, ordering by (category, name)") {
  using corundum::gameplay::item::ItemCategory;

  corundum::world::FlagStore flags;
  flags["item.zzz"] = 1; // Misc — category order puts it second
  flags["item.aaa"] = 1; // Weapon
  flags["item.mmm"] = 1; // Apparel
  flags["item.bbb"] = 1; // Potion

  corundum::gameplay::item::Registry items;
  corundum::gameplay::item::Item weapon;
  weapon.id = "aaa";
  weapon.name = "Axe";
  weapon.category = ItemCategory::Weapon;
  items.add(std::move(weapon));

  corundum::gameplay::item::Item apparel;
  apparel.id = "mmm";
  apparel.name = "Cloak";
  apparel.category = ItemCategory::Apparel;
  items.add(std::move(apparel));

  corundum::gameplay::item::Item potion;
  potion.id = "bbb";
  potion.name = "Draught";
  potion.category = ItemCategory::Potion;
  items.add(std::move(potion));

  const auto lines = corundum::gameplay::screens::build_inventory_lines(flags, items);

  REQUIRE(lines.size() == 4);
  // Enum order is Apparel < Misc < Potion < Weapon; names sort within a category.
  CHECK(lines[0].name == "Cloak");
  CHECK(lines[0].category == ItemCategory::Apparel);
  CHECK(lines[1].name == "zzz");
  CHECK(lines[1].category == ItemCategory::Misc);
  CHECK(lines[2].name == "Draught");
  CHECK(lines[2].category == ItemCategory::Potion);
  CHECK(lines[3].name == "Axe");
  CHECK(lines[3].category == ItemCategory::Weapon);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("inventory_panel_render: category groups draw one header per group, in category order") {
  using corundum::gameplay::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  // Pre-sorted as build_inventory_lines would produce: (category, name).
  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "Cloak"},
      {.category = ItemCategory::Misc, .count = 1, .name = "zzz"},
      {.category = ItemCategory::Potion, .count = 1, .name = "Draught"},
      {.category = ItemCategory::Weapon, .count = 1, .name = "Axe"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome (9) + "Inventory" header + 4 group headers + 4 rows × 2 DrawText (cursor + label).
  REQUIRE(r.log.size() == 9 + 1 + 4 + 8);

  std::vector<std::string> texts;
  for (const auto &call : r.log)
    if (std::holds_alternative<DrawText>(call))
      texts.emplace_back(std::get<DrawText>(call).text);

  CHECK(texts[0] == "Inventory");
  // Each group header immediately precedes its rows (cursor then label), in category
  // order; only the first row (cursor 0) is selected.
  CHECK(texts[1] == "Apparel");
  CHECK(texts[2] == "> ");
  CHECK(texts[3] == "Cloak  x1");
  CHECK(texts[4] == "Misc");
  CHECK(texts[5] == "  ");
  CHECK(texts[6] == "zzz  x1");
  CHECK(texts[7] == "Potion");
  CHECK(texts[8] == "  ");
  CHECK(texts[9] == "Draught  x1");
  CHECK(texts[10] == "Weapon");
  CHECK(texts[11] == "  ");
  CHECK(texts[12] == "Axe  x1");
}

TEST_CASE("inventory_panel_render: Misc-only inventory draws no group header") {
  using corundum::gameplay::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.category = ItemCategory::Misc, .count = 1, .name = "Clutter"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome (9) + "Inventory" header + 1 row × 2. No "Misc" group header.
  REQUIRE(r.log.size() == 9 + 1 + 2);

  const DrawText &header = std::get<DrawText>(r.log[9]);
  CHECK(header.text == "Inventory");
  const DrawText &row_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &row_label = std::get<DrawText>(r.log[11]);
  CHECK(row_cursor.text == "> ");
  CHECK(row_label.text == "Clutter  x1");
}

TEST_CASE("inventory_panel_render: panel width reserves the cursor column and header width") {
  using corundum::gameplay::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};

  // A name long enough to clear k_min_w: label 32 chars (256px) + "> " (16px) = 272 content.
  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "abcdefghijklmnopqrstuvwxyzab"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);

  // measure_text is 8px/char: content = cursor (2) + label (32) = 34 chars, plus 24px pad each side.
  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  CHECK(panel.size.x == (34.f * 8.f) + (24.f * 2.f));
}

TEST_CASE("inventory_panel_render: speaker-sized group header claims its own row height") {
  using corundum::gameplay::item::ItemCategory;
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  corundum::ui::PanelStyle style{};
  style.font_size_body = 10;
  style.line_spacing = 12.f;
  style.font_size_speaker = 40;

  const std::vector<corundum::gameplay::screens::InventoryLine> lines = {
      {.category = ItemCategory::Apparel, .count = 1, .name = "Cloak"},
  };

  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};
  corundum::gameplay::screens::inventory_panel_render(r, style, border, lines, 0, viewport);

  // Chrome(9) + title + group header + cursor + label. Header row height is
  // max(body_line_h, speaker + 4) = max(14, 44) = 44, not the body row height.
  const DrawText &group_header = std::get<DrawText>(r.log[10]);
  const DrawText &row_cursor = std::get<DrawText>(r.log[11]);
  CHECK(group_header.text == "Apparel");
  CHECK(row_cursor.position.y - group_header.position.y == 44.f);

  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  CHECK(panel.size.y == (16.f * 2.f) + 44.f + 10.f + 14.f + 44.f);
}
