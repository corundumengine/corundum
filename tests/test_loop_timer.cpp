// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <chrono>

#include <corundum/core/time/loop_timer.hpp>

using corundum::core::time::LoopTimer;

namespace {

  constexpr float k_fps = 60.f;
  constexpr float k_hitch_cap_seconds = 0.25f;

  // Rewind the timer's clock so the next tick() observes at least `seconds` of elapsed time.
  void rewind_clock(LoopTimer &timer, float seconds) {
    const auto offset =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>(seconds));
    timer.prev_time = std::chrono::steady_clock::now() - offset;
  }

} // namespace

TEST_CASE("LoopTimer — constructor stores the fixed step as one over the target rate") {
  const LoopTimer timer{k_fps};

  CHECK(timer.target_dt == doctest::Approx(1.f / k_fps));
  CHECK(timer.accumulator == doctest::Approx(0.f));
  CHECK(timer.last_frame_dt == doctest::Approx(0.f));
  CHECK(timer.step_count == 0u);
}

TEST_CASE("LoopTimer — set_target_fps retargets the step period") {
  LoopTimer timer{k_fps};
  timer.set_target_fps(30.f);

  CHECK(timer.target_dt == doctest::Approx(1.f / 30.f));
}

TEST_CASE("LoopTimer — set_target_fps preserves carried-over accumulator time") {
  LoopTimer timer{k_fps};
  timer.accumulator = 0.004f;

  timer.set_target_fps(30.f);

  CHECK(timer.accumulator == doctest::Approx(0.004f));
}

TEST_CASE("LoopTimer — step() consumes one step period and counts the step") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt + 0.001f;
  const float accumulator_before = timer.accumulator;

  CHECK(timer.step());
  CHECK(timer.step_count == 1u);
  CHECK(timer.accumulator == doctest::Approx(accumulator_before - timer.target_dt));
}

TEST_CASE("LoopTimer — step() reports false and preserves state when the accumulator is spent") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt * 0.5f;

  CHECK_FALSE(timer.step());
  CHECK(timer.step_count == 0u);
  CHECK(timer.accumulator == doctest::Approx(timer.target_dt * 0.5f));
}

TEST_CASE("LoopTimer — an accumulator holding exactly one period drains to a single step") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt;

  CHECK(timer.step());
  CHECK_FALSE(timer.step());
  CHECK(timer.accumulator == doctest::Approx(0.f));
}

TEST_CASE("LoopTimer — a multi-period hitch drains over multiple steps") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt * 3.f;

  CHECK(timer.step());
  CHECK(timer.step());
  CHECK(timer.step());
  CHECK_FALSE(timer.step());
  CHECK(timer.step_count == 3u);
}

TEST_CASE("LoopTimer — tick() accumulates elapsed wall time") {
  LoopTimer timer{k_fps};
  rewind_clock(timer, 0.01f);

  timer.tick();

  CHECK(timer.last_frame_dt == doctest::Approx(0.01f).epsilon(0.01));
  CHECK(timer.accumulator == doctest::Approx(0.01f).epsilon(0.01));
}

TEST_CASE("LoopTimer — tick() caps a long hitch at 250 ms") {
  LoopTimer timer{k_fps};
  rewind_clock(timer, 2.f);

  timer.tick();

  CHECK(timer.last_frame_dt == doctest::Approx(k_hitch_cap_seconds));
  CHECK(timer.accumulator == doctest::Approx(k_hitch_cap_seconds));
}

TEST_CASE("LoopTimer — tick() advances prev_time so elapsed time is not double-counted") {
  LoopTimer timer{k_fps};
  rewind_clock(timer, 0.01f);

  timer.tick();
  timer.tick();

  // The second tick sees only the microseconds between the two calls, not another 10 ms.
  CHECK(timer.last_frame_dt < 0.01f);
}

TEST_CASE("LoopTimer — alpha() reports the fraction of the way to the next step") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt * 0.5f;

  CHECK(timer.alpha() == doctest::Approx(0.5f));
  CHECK(timer.alpha() < 1.f);
}

TEST_CASE("LoopTimer — alpha() is clamped to 1 once the accumulator covers a step") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt;
  CHECK(timer.alpha() == doctest::Approx(1.f));

  timer.accumulator = timer.target_dt * 5.f;
  CHECK(timer.alpha() == doctest::Approx(1.f));
}

TEST_CASE("LoopTimer — take_steps() bounds a step period the timer alone cannot finish") {
  constexpr int k_budget = 8;
  LoopTimer timer{1e9f}; // target_dt below the accumulator's float resolution: step() never advances
  timer.accumulator = 0.25f;

  CHECK(timer.take_steps(k_budget) == k_budget);    // the budget stops the drain, not step()
  CHECK(timer.accumulator == doctest::Approx(0.f)); // budget exhausted, so the queue is shed
  CHECK_FALSE(timer.step());
}

TEST_CASE("LoopTimer — take_steps() stops early and keeps sub-step time when the queue empties") {
  LoopTimer timer{k_fps};
  timer.accumulator = (timer.target_dt * 3.f) + 0.001f;

  CHECK(timer.take_steps(8) == 3);
  CHECK(timer.step_count == 3u);
  CHECK(timer.accumulator > 0.f);             // the sub-step remainder survives...
  CHECK(timer.accumulator < timer.target_dt); // ...and is what drives interpolation
}

TEST_CASE("LoopTimer — take_steps() consumes nothing when nothing is queued") {
  LoopTimer timer{k_fps};
  timer.accumulator = timer.target_dt * 0.5f;

  CHECK(timer.take_steps(8) == 0);
  CHECK(timer.step_count == 0u);
  CHECK(timer.accumulator == doctest::Approx(timer.target_dt * 0.5f)); // budget not reached: untouched
}

TEST_CASE("LoopTimer — take_steps() sheds the queue once the budget is exhausted") {
  LoopTimer timer{k_fps};
  timer.accumulator = (timer.target_dt * 8.f) + 0.001f; // more queued than the budget allows

  CHECK(timer.take_steps(8) == 8);
  CHECK(timer.accumulator == doctest::Approx(0.f));
  CHECK_FALSE(timer.step());
}
