// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/quest/quest.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/ui/hud_strip.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <string>
#include <utility>

namespace {

  using corundum::world::FlagStore;

  corundum::quest::Quest make_active_quest() {
    corundum::quest::Quest q;
    q.quest_id = "ember";
    q.name = "Ember of Greyhollow";
    q.stages.push_back({.name = "start", .objectives = {{.text = "Find the shrine"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    return q;
  }

} // namespace

TEST_CASE("build_hud_strip: gold with no started quests") {
  corundum::quest::Registry quests;
  FlagStore flags;
  flags["gold"] = 50;

  const auto data = corundum::ui::build_hud_strip(flags, quests);
  CHECK(data.gold == 50);
  CHECK_FALSE(data.has_quest);
}

TEST_CASE("build_hud_strip: reports the active quest's name and current objective") {
  corundum::quest::Registry quests;
  quests.add(make_active_quest());

  FlagStore flags;
  flags["gold"] = 12;
  corundum::quest::start(*quests.find("ember"), flags);

  const auto data = corundum::ui::build_hud_strip(flags, quests);
  CHECK(data.gold == 12);
  CHECK(data.has_quest);
  CHECK(data.quest_name == "Ember of Greyhollow");
  CHECK(data.objective == "Find the shrine");
}

TEST_CASE("build_hud_strip: a completed quest is not reported as the active objective") {
  corundum::quest::Registry quests;
  quests.add(make_active_quest());

  FlagStore flags;
  flags["gold"] = 0;
  corundum::quest::start(*quests.find("ember"), flags);
  flags[corundum::quest::quest_flag_key("ember")] = 2; // resolved stage

  const auto data = corundum::ui::build_hud_strip(flags, quests);
  CHECK_FALSE(data.has_quest);
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("hud_strip_render: chrome plus one line with gold and the active objective") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  corundum::ui::HudStripData data{};
  data.gold = 50;
  data.has_quest = true;
  data.quest_name = "Ember";
  data.objective = "Find the shrine";

  RecordingRenderer r;
  corundum::ui::hud_strip_render(r, {}, make_border(), data);

  // Chrome (1 rect + 8 sprites) then the single text line.
  REQUIRE(r.log.size() == 10);
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[0]));
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<corundum::platform::DrawSprite>(r.log[i]));

  const auto &line = std::get<corundum::platform::DrawText>(r.log[9]);
  CHECK(line.text == "Gold: 50    Ember - Find the shrine");
}
