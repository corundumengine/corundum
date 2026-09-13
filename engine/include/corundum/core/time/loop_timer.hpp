// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace corundum::core::time {

  /// Fixed-timestep accumulator for platform-independent game loop timing.
  struct LoopTimer {
    /** @brief Seconds of unprocessed time carried between frames. */
    float accumulator = 0.f;
    /** @brief Wall-clock instant of the previous tick(). */
    std::chrono::steady_clock::time_point prev_time = std::chrono::steady_clock::now();
    /** @brief Length of one fixed step in seconds, derived from the target rate. */
    float target_dt = 0.f;

    /** @brief Elapsed time measured by the last tick(), after the hitch cap. */
    float last_frame_dt = 0.f;
    /** @brief Monotonic fixed-step counter. Prefer over elapsed_time for cooldowns and scripted events. */
    uint64_t step_count = 0;

    /** @brief Construct a timer targeting @p fps fixed updates per second.
     *  @param[in] fps Desired fixed-update rate, in steps per second.
     */
    explicit LoopTimer(float fps) noexcept : target_dt(1.f / fps) {}

    /** @brief Retarget the simulation rate at runtime, e.g. after loading config.
     *
     *  The accumulator is left untouched, so no time is added or lost across the change: the
     *  carried remainder is simply measured against the new period.
     *
     *  @param[in] fps New fixed-update rate, in steps per second.
     */
    void set_target_fps(float fps) noexcept {
      target_dt = 1.f / fps;
    }

    /** @brief Sample wall time and add it to the accumulator; call once per frame.
     *
     *  Elapsed time is capped at 250 ms so a hitch — debugger breakpoint, asset load, OS
     *  preemption — cannot queue an unbounded burst of fixed steps.
     */
    void tick() noexcept {
      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const float raw_frame_time = std::chrono::duration<float>(now - prev_time).count();
      const float capped = std::min(raw_frame_time, 0.25f);

      last_frame_dt = capped;
      accumulator += capped;
      prev_time = now;
    }

    /** @brief Consume one fixed step from the accumulator.
     *  @return true when a step was consumed; false when the accumulator is spent.
     */
    [[nodiscard]] bool step() noexcept {
      if (accumulator < target_dt) {
        [[unlikely]] return false;
      }

      accumulator -= target_dt;
      ++step_count;
      return true;
    }

    /** @brief Consume up to @p budget fixed steps, shedding time queued beyond the budget.
     *
     *  The budget bounds catch-up work per frame. If it is exhausted with time still queued the
     *  remainder is dropped, so the simulation falls behind wall time instead of hitting the same
     *  budget on every later frame. Capping and shedding are one operation: shedding separately
     *  from the cap is what lets a backlog grow without bound.
     *
     *  @param[in] budget Maximum number of steps to consume; must be > 0.
     *  @return Number of steps consumed, in [0, budget].
     */
    [[nodiscard]] int take_steps(int budget) noexcept {
      int taken = 0;
      while (taken < budget && step()) {
        ++taken;
      }

      if (taken == budget)
        accumulator = 0.f;

      return taken;
    }

    /** @brief Interpolation factor for render smoothing.
     *  @return How far the accumulator has progressed toward the next fixed step, in [0, 1];
     *          1 once it fully covers a step or more.
     *  @pre target_dt must be > 0.
     */
    [[nodiscard]] float alpha() const noexcept {
      return std::min(accumulator / target_dt, 1.f);
    }
  };

} // namespace corundum::core::time
