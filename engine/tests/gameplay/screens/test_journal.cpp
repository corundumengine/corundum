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
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

  using corundum::world::FlagStore;

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

} // namespace

TEST_CASE("build_journal_entries: no started quests yields an empty list") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_two_stage_quest("ember", "Ember of Greyhollow"));

  const auto entries = corundum::gameplay::screens::build_journal_entries(quests, {});
  CHECK(entries.empty());
}

TEST_CASE("build_journal_entries: name, lifecycle, and current objective per started quest") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_two_stage_quest("ember", "Ember of Greyhollow"));
  quests.add(make_two_stage_quest("salt", "Debt of Salt"));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  corundum::gameplay::quest::start(*quests.find("salt"), flags);
  complete(flags, "salt");

  const auto entries = corundum::gameplay::screens::build_journal_entries(quests, flags);
  REQUIRE(entries.size() == 2);

  // Active sorts before Completed, regardless of id order.
  CHECK(entries[0].name == "Ember of Greyhollow");
  CHECK(entries[0].lifecycle == corundum::gameplay::quest::Lifecycle::Active);
  CHECK(entries[0].objective == "Find the shrine");

  CHECK(entries[1].name == "Debt of Salt");
  CHECK(entries[1].lifecycle == corundum::gameplay::quest::Lifecycle::Completed);
}

TEST_CASE("build_journal_entries: current objective skips done objectives and falls back to the first") {
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

  // First objective not yet done → reported even though "Second" is also pending.
  CHECK(corundum::gameplay::screens::build_journal_entries(quests, flags)[0].objective == "First");

  flags["first_done"] = 1;
  CHECK(corundum::gameplay::screens::build_journal_entries(quests, flags)[0].objective == "Second");
}

TEST_CASE("build_journal_entries: groups by lifecycle section order, then by name") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_two_stage_quest("a_completed", "A Completed"));
  quests.add(make_two_stage_quest("b_active", "B Active"));

  // Build the failed quest with a genuine failed ending stage.
  corundum::gameplay::quest::Quest failed;
  failed.quest_id = "c_failed";
  failed.name = "C Failed";
  failed.stages.push_back({.name = "start", .sequence = 1});
  failed.stages.push_back({.failed = true, .name = "lost", .resolved = true, .sequence = 2});
  quests.add(std::move(failed));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("a_completed"), flags);
  complete(flags, "a_completed");
  corundum::gameplay::quest::start(*quests.find("b_active"), flags);
  corundum::gameplay::quest::start(*quests.find("c_failed"), flags);
  flags[corundum::gameplay::quest::quest_flag_key("c_failed")] = 2;

  const auto entries = corundum::gameplay::screens::build_journal_entries(quests, flags);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].lifecycle == corundum::gameplay::quest::Lifecycle::Active);
  CHECK(entries[1].lifecycle == corundum::gameplay::quest::Lifecycle::Completed);
  CHECK(entries[2].lifecycle == corundum::gameplay::quest::Lifecycle::Failed);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("journal_panel_render: chrome, title, lifecycle headers, then one option row per quest") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  const std::vector<corundum::gameplay::screens::JournalEntry> entries = {
      {.name = "Ember", .objective = "Find the shrine", .lifecycle = corundum::gameplay::quest::Lifecycle::Active},
      {.name = "Salt", .objective = "", .lifecycle = corundum::gameplay::quest::Lifecycle::Completed},
  };

  corundum::gameplay::screens::journal_panel_render(r, style, make_border(), entries, 0, {.x = 1280.f, .y = 720.f});

  // Chrome (1 rect + 8 sprites) + title + footer hint + "Active" header
  // + (cursor + name + objective) + "Completed" header + (cursor + name).
  REQUIRE(r.log.size() == 9 + 1 + 1 + 1 + 3 + 1 + 2);
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[0]));

  std::vector<std::string> texts;
  for (const auto &call : r.log)
    if (std::holds_alternative<corundum::platform::DrawText>(call))
      texts.emplace_back(std::get<corundum::platform::DrawText>(call).text);

  REQUIRE(texts.size() == 9);
  CHECK(texts[0] == "Journal");
  CHECK(texts[1] == "Esc Close");
  CHECK(texts[2] == "Active");
  CHECK(texts[3] == "> ");
  CHECK(texts[4] == "Ember");
  CHECK(texts[5] == "Find the shrine");
  CHECK(texts[6] == "Completed");
  CHECK(texts[7] == "  ");
  CHECK(texts[8] == "Salt");
}

TEST_CASE("journal_panel_render: the footer hint follows the last-used device") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<corundum::gameplay::screens::JournalEntry> entries{};
  corundum::gameplay::screens::journal_panel_render(r, style, make_border(), entries, 0, {.x = 1280.f, .y = 720.f},
                                                    corundum::input::InputDevice::Gamepad);

  const auto &footer = std::get<corundum::platform::DrawText>(r.log[10]);
  CHECK(footer.text == "B Close");
}

TEST_CASE("journal_panel_render: empty journal renders the placeholder line") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const std::vector<corundum::gameplay::screens::JournalEntry> entries{};
  corundum::gameplay::screens::journal_panel_render(r, style, make_border(), entries, 0, {.x = 1280.f, .y = 720.f});

  REQUIRE(r.log.size() == 9 + 1 + 1 + 1);
  const auto &title = std::get<corundum::platform::DrawText>(r.log[9]);
  const auto &empty = std::get<corundum::platform::DrawText>(r.log[11]);
  CHECK(title.text == "Journal");
  CHECK(empty.text == "(no quests)");
}
