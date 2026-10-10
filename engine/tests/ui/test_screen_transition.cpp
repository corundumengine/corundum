// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/platform/renderer.hpp>
#include <corundum/ui/screen_transition.hpp>

#include "recording_renderer.hpp"

#include <variant>

namespace {

  using corundum::ui::ScreenTransition;
  using corundum::ui::TransitionPhase;

  constexpr float k_duration = ScreenTransition::k_fade_duration_seconds;

} // namespace

TEST_CASE("ScreenTransition: idle does not update") {
  ScreenTransition transition;
  CHECK(transition.phase() == TransitionPhase::None);
  CHECK_FALSE(transition.active());
  CHECK_FALSE(transition.at_black());
  CHECK_EQ(transition.alpha(), 0.f);

  transition.update(1.f);
  CHECK(transition.phase() == TransitionPhase::None);
  CHECK_EQ(transition.alpha(), 0.f);
}

TEST_CASE("ScreenTransition: begin_fade_out rises from clear to black and stops") {
  ScreenTransition transition;
  transition.begin_fade_out();
  CHECK(transition.phase() == TransitionPhase::FadingOut);
  CHECK(transition.active());
  CHECK_EQ(transition.alpha(), 0.f);

  transition.update(k_duration / 2.f);
  CHECK(transition.phase() == TransitionPhase::FadingOut);
  CHECK(transition.alpha() == doctest::Approx(0.5f));

  transition.update(k_duration / 2.f);
  CHECK(transition.phase() == TransitionPhase::None);
  CHECK(transition.at_black());
  CHECK_EQ(transition.alpha(), 1.f);
}

TEST_CASE("ScreenTransition: begin_fade_in jumps to black and clears") {
  ScreenTransition transition;
  transition.begin_fade_in();
  CHECK(transition.phase() == TransitionPhase::FadingIn);
  CHECK(transition.at_black());
  CHECK(transition.alpha() == doctest::Approx(1.f));

  transition.update(k_duration / 2.f);
  CHECK(transition.phase() == TransitionPhase::FadingIn);
  CHECK(transition.alpha() == doctest::Approx(0.5f));

  transition.update(k_duration / 2.f);
  CHECK(transition.phase() == TransitionPhase::None);
  CHECK_FALSE(transition.at_black());
  CHECK_EQ(transition.alpha(), 0.f);
}

TEST_CASE("ScreenTransition: alpha stops at the target rather than overshooting") {
  ScreenTransition transition;
  transition.begin_fade_out();
  transition.update(k_duration * 4.f);
  CHECK_EQ(transition.alpha(), 1.f);
  CHECK(transition.phase() == TransitionPhase::None);
}

TEST_CASE("ScreenTransition: reset clears instantly") {
  ScreenTransition transition;
  transition.begin_fade_out();
  transition.update(k_duration / 2.f);

  transition.reset();
  CHECK(transition.phase() == TransitionPhase::None);
  CHECK_EQ(transition.alpha(), 0.f);
  CHECK_FALSE(transition.active());
}

TEST_CASE("screen transition overlay: clear alpha emits nothing") {
  corundum::test::RecordingRenderer renderer;
  const corundum::ui::ScreenTransition transition;

  corundum::ui::render_screen_transition(renderer, transition, {.x = 320.f, .y = 240.f});
  CHECK(renderer.log.empty());
}

TEST_CASE("screen transition overlay: partial alpha emits one black rect covering the viewport") {
  corundum::test::RecordingRenderer renderer;
  corundum::ui::ScreenTransition transition;
  transition.begin_fade_out();
  transition.update(k_duration / 2.f);

  corundum::ui::render_screen_transition(renderer, transition, {.x = 320.f, .y = 240.f});
  REQUIRE(renderer.log.size() == 1);
  const auto *rect = std::get_if<corundum::platform::DrawRect>(&renderer.log.front());
  REQUIRE(rect != nullptr);
  CHECK_EQ(rect->position.x, 0.f);
  CHECK_EQ(rect->position.y, 0.f);
  CHECK_EQ(rect->size.x, 320.f);
  CHECK_EQ(rect->size.y, 240.f);
  CHECK_EQ(rect->colour.r, 0);
  CHECK_EQ(rect->colour.g, 0);
  CHECK_EQ(rect->colour.b, 0);
  CHECK_EQ(rect->colour.a, 127);
}
