// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <variant>

namespace {

  using corundum::world::FlagStore;

  corundum::gameplay::quest::Quest make_active_quest() {
    corundum::gameplay::quest::Quest q;
    q.quest_id = "ember";
    q.name = "Ember of Greyhollow";
    q.stages.push_back({.name = "start", .objectives = {{.text = "Find the shrine"}}, .sequence = 1});
    q.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
    return q;
  }

} // namespace

TEST_CASE("build_hud_strip: gold with no started quests") {
  const corundum::gameplay::quest::Registry quests;
  FlagStore flags;
  flags["gold"] = 50;

  const auto data = corundum::gameplay::screens::build_hud_strip(flags, quests);
  CHECK(data.gold == 50);
  CHECK_FALSE(data.has_quest);
}

TEST_CASE("build_hud_strip: reports the active quest's name and current objective") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_active_quest());

  FlagStore flags;
  flags["gold"] = 12;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);

  const auto data = corundum::gameplay::screens::build_hud_strip(flags, quests);
  CHECK(data.gold == 12);
  CHECK(data.has_quest);
  CHECK(data.quest_name == "Ember of Greyhollow");
  CHECK(data.objective == "Find the shrine");
}

TEST_CASE("build_hud_strip: a completed quest is not reported as the active objective") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_active_quest());

  FlagStore flags;
  flags["gold"] = 0;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  flags[corundum::gameplay::quest::quest_flag_key("ember")] = 2; // resolved stage

  const auto data = corundum::gameplay::screens::build_hud_strip(flags, quests);
  CHECK_FALSE(data.has_quest);
}

TEST_CASE("build_hud_strip: the tracked quest wins over the first active one") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_active_quest());
  corundum::gameplay::quest::Quest salt;
  salt.quest_id = "salt";
  salt.name = "Debt of Salt";
  salt.stages.push_back({.name = "start", .objectives = {{.text = "Pay it"}}, .sequence = 1});
  salt.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  quests.add(std::move(salt));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  corundum::gameplay::quest::start(*quests.find("salt"), flags);

  // Alphabetically "Debt of Salt" would lead; tracking Ember pins it instead.
  corundum::world::set_flag(flags, corundum::gameplay::quest::tracked_flag_key("ember"));
  const auto tracked = corundum::gameplay::screens::build_hud_strip(flags, quests);
  CHECK(tracked.has_quest);
  CHECK(tracked.quest_name == "Ember of Greyhollow");
}

TEST_CASE("build_hud_strip: falls back to the first active quest once the tracked quest completes") {
  corundum::gameplay::quest::Registry quests;
  quests.add(make_active_quest());
  corundum::gameplay::quest::Quest salt;
  salt.quest_id = "salt";
  salt.name = "Debt of Salt";
  salt.stages.push_back({.name = "start", .objectives = {{.text = "Pay it"}}, .sequence = 1});
  salt.stages.push_back({.name = "done", .resolved = true, .sequence = 2});
  quests.add(std::move(salt));

  FlagStore flags;
  corundum::gameplay::quest::start(*quests.find("ember"), flags);
  corundum::gameplay::quest::start(*quests.find("salt"), flags);
  corundum::world::set_flag(flags, corundum::gameplay::quest::tracked_flag_key("ember"));
  flags[corundum::gameplay::quest::quest_flag_key("ember")] = 2; // tracked quest now complete

  const auto data = corundum::gameplay::screens::build_hud_strip(flags, quests);
  CHECK(data.has_quest);
  CHECK(data.quest_name == "Debt of Salt");
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("hud_strip_render: an opaque panel plus one line with gold and the objective") {
  using corundum::test::make_border;
  using corundum::test::RecordingRenderer;

  corundum::gameplay::screens::HudStripData data{};
  data.gold = 50;
  data.has_quest = true;
  data.quest_name = "Ember";
  data.objective = "Find the shrine";

  RecordingRenderer r;
  corundum::ui::PanelStyle style{};
  style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Ui)] = {.ids = {1u, 2u, 3u, 4u}};
  style.fonts[static_cast<std::size_t>(corundum::ui::FontRole::Quest)] = {.ids = {5u, 6u, 7u, 8u}};
  corundum::gameplay::screens::hud_strip_render(r, style, make_border(), data);

  // The fill rect, the border's 8 sprites, then the line: gold (UI) + separator + the tracked
  // quest name and objective (Quest), each its own draw so the quest portion can use its role.
  REQUIRE(r.log.size() == 9 + 5);
  const auto &fill = std::get<corundum::platform::DrawRect>(r.log[0]);
  CHECK(fill.colour.a == 255); // opaque: a translucent fill would tint the world behind it
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<corundum::platform::DrawSprite>(r.log[i]));

  const auto draw_text = [&](std::size_t i) -> const corundum::platform::DrawText & {
    return std::get<corundum::platform::DrawText>(r.log[i]);
  };
  CHECK(draw_text(9).text == "Gold: 50");
  CHECK(draw_text(10).text == "    ");
  CHECK(draw_text(11).text == "Ember");
  CHECK(draw_text(12).text == " - ");
  CHECK(draw_text(13).text == "Find the shrine");

  // Gold uses the UI regular face; the quest name is bold and the objective regular, both Quest.
  CHECK(draw_text(9).font_id == 1u);
  CHECK(draw_text(11).font_id == 6u);
  CHECK(draw_text(13).font_id == 5u);
}
