// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/ui/toast.hpp>

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <string>

TEST_CASE("ToastQueue: notify enqueues text with the default colour and TTL") {
  corundum::ui::ToastQueue queue;
  CHECK(queue.empty());

  queue.notify("Quest started: Ember");

  REQUIRE(queue.size() == 1);
  CHECK(queue.at(0).text == "Quest started: Ember");
  CHECK(queue.at(0).colour.r == corundum::ui::k_toast_default_colour.r);
  CHECK(queue.at(0).remaining == doctest::Approx(corundum::ui::k_toast_ttl_seconds));
}

TEST_CASE("ToastQueue: notify accepts an explicit colour") {
  corundum::ui::ToastQueue queue;
  queue.notify("Quest complete", corundum::ui::k_toast_complete_colour);

  REQUIRE(queue.size() == 1);
  CHECK(queue.at(0).colour.g == corundum::ui::k_toast_complete_colour.g);
}

TEST_CASE("ToastQueue: update ages toasts and culls the expired ones") {
  corundum::ui::ToastQueue queue;
  queue.notify("first");
  queue.notify("second");

  queue.update(1.f);
  REQUIRE(queue.size() == 2);
  CHECK(queue.at(0).remaining == doctest::Approx(corundum::ui::k_toast_ttl_seconds - 1.f));

  queue.update(corundum::ui::k_toast_ttl_seconds);
  CHECK(queue.empty());
}

TEST_CASE("ToastQueue: a burst past the visible cap drops the oldest entries") {
  corundum::ui::ToastQueue queue;
  for (std::size_t i = 0; i < corundum::ui::k_toast_max_visible + 2; ++i)
    queue.notify("toast " + std::to_string(i));

  REQUIRE(queue.size() == corundum::ui::k_toast_max_visible);
  // The first two were evicted, so the oldest survivor is "toast 2".
  CHECK(queue.at(0).text == "toast 2");
}

TEST_CASE("ToastQueue: a repeated message refreshes the existing toast") {
  corundum::ui::ToastQueue queue;
  queue.notify("Not enough gold");
  queue.update(2.f);
  REQUIRE(queue.size() == 1);
  CHECK(queue.at(0).remaining == doctest::Approx(corundum::ui::k_toast_ttl_seconds - 2.f));

  queue.notify("Not enough gold");
  REQUIRE(queue.size() == 1);
  CHECK(queue.at(0).remaining == doctest::Approx(corundum::ui::k_toast_ttl_seconds));
}

TEST_CASE("ToastQueue: the same text in a different colour is not deduplicated") {
  corundum::ui::ToastQueue queue;
  queue.notify("Quest updated");
  queue.notify("Quest updated", corundum::ui::k_toast_failed_colour);

  REQUIRE(queue.size() == 2);
}

TEST_CASE("ToastQueue: clear drops every toast") {
  corundum::ui::ToastQueue queue;
  queue.notify("a");
  queue.notify("b");

  queue.clear();
  CHECK(queue.empty());
}

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("ToastQueue: render draws a background rect and one text per live toast") {
  using corundum::test::RecordingRenderer;

  corundum::ui::ToastQueue queue;
  queue.notify("Quest started: Ember");
  queue.notify("Item received: hammer", corundum::ui::k_toast_updated_colour);

  RecordingRenderer r;
  queue.render(r, {}, {.x = 1280.f, .y = 720.f});

  // One DrawRect per toast (no border texture passed), plus one DrawText each.
  REQUIRE(r.log.size() == 4);
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[0]));
  CHECK(std::holds_alternative<corundum::platform::DrawText>(r.log[1]));
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[2]));
  CHECK(std::holds_alternative<corundum::platform::DrawText>(r.log[3]));

  const auto &first = std::get<corundum::platform::DrawText>(r.log[1]);
  const auto &second = std::get<corundum::platform::DrawText>(r.log[3]);
  CHECK(first.text == "Quest started: Ember");
  CHECK(second.text == "Item received: hammer");
  CHECK(first.colour.r == corundum::ui::k_toast_default_colour.r);
  CHECK(second.colour.b == corundum::ui::k_toast_updated_colour.b);

  // Oldest is stacked above the newest, so the second row is lower on screen.
  CHECK(second.position.y > first.position.y);
}

TEST_CASE("ToastQueue: render is a no-op with no live toasts") {
  using corundum::test::RecordingRenderer;

  corundum::ui::ToastQueue queue;
  RecordingRenderer r;
  queue.render(r, {}, {.x = 1280.f, .y = 720.f});
  CHECK(r.log.empty());
}
