// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/ui_stack.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace {

  using corundum::gameplay::screens::hub_tab_label;
  using corundum::gameplay::screens::hub_tab_strip;
  using corundum::test::RecordingRenderer;
  using corundum::world::GameMode;
  namespace screens = corundum::gameplay::screens;

} // namespace

TEST_CASE("hub_tab_label: names the four tabs and nothing else") {
  CHECK(hub_tab_label(screens::Inventory) == "Inventory");
  CHECK(hub_tab_label(screens::Journal) == "Journal");
  CHECK(hub_tab_label(screens::Codex) == "Codex");
  CHECK(hub_tab_label(screens::Map) == "Map");
  CHECK(hub_tab_label(GameMode::Exploring).empty());
  CHECK(hub_tab_label(GameMode::Menu).empty());
}

// doctest's CHECK macros expand to control flow, so the assertion count — not the test's
// logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("hub_tab_strip: tabs are ordered, non-overlapping and centered") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const auto strip = hub_tab_strip(r, style, {.x = 1280.f, .y = 720.f});

  REQUIRE(strip.tabs.size() == 4);
  CHECK(strip.tabs[0].pos.x < strip.tabs[1].pos.x);
  CHECK(strip.tabs[1].pos.x < strip.tabs[2].pos.x);
  CHECK(strip.tabs[2].pos.x < strip.tabs[3].pos.x);

  for (std::size_t i = 0; i + 1 < strip.tabs.size(); ++i)
    CHECK(strip.tabs[i].pos.x + strip.tabs[i].width < strip.tabs[i + 1].pos.x);

  const float first = strip.tabs.front().pos.x;
  const float last = strip.tabs.back().pos.x + strip.tabs.back().width;
  CHECK(first > 0.f);
  CHECK(last < 1280.f);
  CHECK(first == doctest::Approx((1280.f - (last - first)) * 0.5f));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("hub_tab_strip_render: draws the bumper hints, all four labels, and highlights active") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  corundum::gameplay::screens::hub_tab_strip_render(r, style, screens::Codex, {.x = 1280.f, .y = 720.f},
                                                    corundum::input::InputDevice::Keyboard);

  std::vector<std::string> texts;
  for (const auto &call : r.log)
    if (std::holds_alternative<corundum::platform::DrawText>(call))
      texts.emplace_back(std::get<corundum::platform::DrawText>(call).text);

  REQUIRE(texts.size() == 6);
  CHECK(texts[0] == "[");
  CHECK(texts[1] == "Inventory");
  CHECK(texts[2] == "Journal");
  CHECK(texts[3] == "Codex");
  CHECK(texts[4] == "Map");
  CHECK(texts[5] == "]");

  // The active tab uses the speaker colour; the others use the dim choice colour.
  const auto colour_of = [&](std::size_t index) { return std::get<corundum::platform::DrawText>(r.log[index]).colour; };
  const auto same_rgb = [](const corundum::core::math::Colour &a, const corundum::core::math::Colour &b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
  };
  CHECK(same_rgb(colour_of(3), style.speaker)); // Codex
  CHECK(same_rgb(colour_of(1), style.choice));  // Inventory
}

TEST_CASE("hub_tab_strip_render: the bumper hints follow the last-used device") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  corundum::gameplay::screens::hub_tab_strip_render(r, style, screens::Inventory, {.x = 1280.f, .y = 720.f},
                                                    corundum::input::InputDevice::Gamepad);

  const auto &prev = std::get<corundum::platform::DrawText>(r.log.front());
  const auto &next = std::get<corundum::platform::DrawText>(r.log.back());
  CHECK(prev.text == "L1");
  CHECK(next.text == "R1");
}
