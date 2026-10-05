// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/dialogue/compiled_expr.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

  using corundum::world::FlagStore;
  namespace screens = corundum::gameplay::screens;

  /// Two-stage quest: a non-resolved start with one bare objective, then a resolved end.
  corundum::gameplay::quest::Quest make_two_stage_quest(std::string id, std::string name) {
    corundum::gameplay::quest::Quest q;
    q.quest_id = std::move(id);
    q.name = std::move(name);
    q.stages.push_back({.name = "start", .objectives = {{.text = "Find the shrine"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    return q;
  }

  /// Advance @p id to its second (resolved) stage.
  void complete(FlagStore &flags, std::string_view quest_id) {
    flags[corundum::gameplay::quest::quest_flag_key(quest_id)] = 2;
  }

  /// Every text payload the recording renderer received, in draw order.
  std::vector<std::string> recorded_texts(const corundum::test::RecordingRenderer &r) {
    std::vector<std::string> texts;
    for (const auto &call : r.log)
      if (std::holds_alternative<corundum::platform::DrawText>(call))
        texts.emplace_back(std::get<corundum::platform::DrawText>(call).text);
    return texts;
  }

  /// The recorded DrawText with exactly @p text, or nullptr.
  const corundum::platform::DrawText *find_text(const corundum::test::RecordingRenderer &r, std::string_view text) {
    for (const auto &call : r.log) {
      if (const auto *drawn = std::get_if<corundum::platform::DrawText>(&call); drawn != nullptr && drawn->text == text)
        return drawn;
    }
    return nullptr;
  }

} // namespace

TEST_CASE("build_journal_entries: no started quests yields an empty list") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_two_stage_quest("ember", "Ember of Greyhollow"));

  const auto entries = screens::build_journal_entries(quests, {}, screens::JournalTab::Active);
  CHECK(entries.empty());
}

TEST_CASE("build_journal_entries: filters to the requested lifecycle tab") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_two_stage_quest("ember", "Ember of Greyhollow"));
  quests.add(make_two_stage_quest("salt", "Debt of Salt"));
  quests.add(make_two_stage_quest("ash", "Ashen Road"));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  corundum::gameplay::quest::start(*quests.find("salt"), flags);
  corundum::gameplay::quest::start(*quests.find("ash"), flags);
  complete(flags, "salt");

  const auto active = screens::build_journal_entries(quests, flags, screens::JournalTab::Active);
  REQUIRE(active.size() == 2);
  CHECK(active[0].name == "Ashen Road");
  CHECK(active[0].lifecycle == corundum::gameplay::quest::Lifecycle::Active);
  CHECK(active[0].objective == "Find the shrine");

  const auto completed = screens::build_journal_entries(quests, flags, screens::JournalTab::Completed);
  REQUIRE(completed.size() == 1);
  CHECK(completed[0].name == "Debt of Salt");

  const auto failed = screens::build_journal_entries(quests, flags, screens::JournalTab::Failed);
  CHECK(failed.empty());
}

TEST_CASE("build_journal_entries: carries the full objective checklist and tracked mark") {
  corundum::gameplay::quest::Registry quests;
  corundum::gameplay::quest::Quest q;
  q.quest_id = "ember";
  q.name = "Ember";
  q.stages.push_back({
      .name = "start",
      .objectives =
          {
              {.done_condition = *corundum::gameplay::dialogue::compile("first_done >= 1"), .text = "First"},
              {.text = "Second"},
          },
      .sequence = 1,
  });
  q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  quests.add(std::move(q));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  flags["first_done"] = 1;
  corundum::world::set_flag(flags, corundum::gameplay::quest::tracked_flag_key("ember"));

  const auto entries = screens::build_journal_entries(quests, flags, screens::JournalTab::Active);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].tracked);
  CHECK(entries[0].objective == "Second");
  REQUIRE(entries[0].objectives.size() == 2);
  CHECK(entries[0].objectives[0].text == "First");
  CHECK(entries[0].objectives[0].done);
  CHECK(entries[0].objectives[1].text == "Second");
  CHECK_FALSE(entries[0].objectives[1].done);
}

TEST_CASE("cycle_journal_tab: wraps both ways") {
  CHECK(screens::cycle_journal_tab(screens::JournalTab::Active, 1) == screens::JournalTab::Completed);
  CHECK(screens::cycle_journal_tab(screens::JournalTab::Failed, 1) == screens::JournalTab::Active);
  CHECK(screens::cycle_journal_tab(screens::JournalTab::Active, -1) == screens::JournalTab::Failed);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("journal_panel_render: sub-tab strip, then the highlighted quest's checklist") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  const std::vector<screens::JournalEntry> entries = {
      {
          .id = "ember",
          .name = "Ember",
          .objective = "Find the shrine",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
          .objectives = {{.text = "Find the shrine", .done = false}, {.text = "Return home", .done = true}},
      },
      {
          .id = "salt",
          .name = "Salt",
          .objective = "",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
      },
  };
  const screens::JournalState state{};

  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f});

  // Chrome (1 rect + 8 sprites) + title + 3 sub-tabs + footer + (cursor + name) + 2 checklists
  // (each its mark + text) + (cursor + name) for the unhighlighted second row.
  REQUIRE(r.log.size() == 9 + 1 + 3 + 1 + 2 + 4 + 2);
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[0]));

  const std::vector<std::string> texts = recorded_texts(r);
  REQUIRE(texts.size() == 13);
  CHECK(texts[0] == "Journal");
  CHECK(texts[1] == "Active");
  CHECK(texts[2] == "Completed");
  CHECK(texts[3] == "Failed");
  CHECK(texts[4] == "Enter Track   . Tabs   Esc Close");
  CHECK(texts[5] == "> ");
  CHECK(texts[6] == "Ember");
  CHECK(texts[7] == "[ ] ");
  CHECK(texts[8] == "Find the shrine");
  CHECK(texts[9] == "[x] ");
  CHECK(texts[10] == "Return home");
  CHECK(texts[11] == "  ");
  CHECK(texts[12] == "Salt");
}

TEST_CASE("journal_panel_render: a tracked quest carries a mark, others keep the one objective line") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  const std::vector<screens::JournalObjective> shrine_objectives = {
      {.text = "Find the shrine", .done = false},
  };
  const std::vector<screens::JournalEntry> entries = {
      {
          .id = "ember",
          .name = "Ember",
          .objective = "Find the shrine",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
          .tracked = true,
          .objectives = shrine_objectives,
      },
      {
          .id = "salt",
          .name = "Salt",
          .objective = "Pay the debt",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
      },
  };
  const screens::JournalState state{};

  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f});

  const std::vector<std::string> texts = recorded_texts(r);
  // Title, 3 sub-tabs, footer, then row 0 (tracked + highlighted checklist), row 1 single objective.
  REQUIRE(texts.size() == 12);
  CHECK(texts[5] == "> ");
  CHECK(texts[6] == "* Ember");
  CHECK(texts[7] == "[ ] ");
  CHECK(texts[8] == "Find the shrine");
  CHECK(texts[9] == "  ");
  CHECK(texts[10] == "Salt");
  CHECK(texts[11] == "Pay the debt");
}

TEST_CASE("journal_panel_render: the footer hint follows the last-used device") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<screens::JournalEntry> entries{};
  const screens::JournalState state{};
  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f},
                                corundum::input::InputDevice::Gamepad);

  const std::vector<std::string> texts = recorded_texts(r);
  CHECK(texts[4] == "A Track   R2 Tabs   B Close");
}

TEST_CASE("journal_panel_render: empty tab renders the placeholder line") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<screens::JournalEntry> entries{};
  const screens::JournalState state{};
  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f});

  REQUIRE(r.log.size() == 9 + 1 + 3 + 1 + 1);
  const std::vector<std::string> texts = recorded_texts(r);
  REQUIRE(texts.size() == 6);
  CHECK(texts[0] == "Journal");
  CHECK(texts[5] == "(no quests)");
}

TEST_CASE("journal_panel_render: objective markup draws per-style Quest segments") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  corundum::ui::PanelStyle style{};
  style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Quest)] = {.ids = {5u, 6u, 7u, 8u}};

  const std::vector<screens::JournalEntry> entries = {
      {
          .id = "ember",
          .name = "Ember",
          .objective = "",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
          .objectives = {{.text = "**Find** the shrine", .done = false}},
      },
  };
  const screens::JournalState state{};

  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f});

  const auto *bold = find_text(r, "Find");
  const auto *regular = find_text(r, " the shrine");
  REQUIRE(bold != nullptr);
  REQUIRE(regular != nullptr);
  CHECK(bold->font_id == 6u);    // Quest Bold
  CHECK(regular->font_id == 5u); // Quest Regular
}

TEST_CASE("journal_panel_render: the quest list row uses the Quest family") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  corundum::ui::PanelStyle style{};
  style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Quest)] = {.ids = {5u, 6u, 7u, 8u}};

  const std::vector<screens::JournalEntry> entries = {
      {
          .id = "ember",
          .name = "Ember",
          .objective = "",
          .lifecycle = corundum::gameplay::quest::Lifecycle::Active,
      },
  };
  const screens::JournalState state{};

  screens::journal_panel_render(r, style, make_border(), entries, state, {.x = 1280.f, .y = 720.f});

  const auto *name = find_text(r, "Ember");
  REQUIRE(name != nullptr);
  CHECK(name->font_id == 6u); // Quest Bold
}
