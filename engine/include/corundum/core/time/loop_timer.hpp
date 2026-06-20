#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace corundum::core::time {

  /// Fixed-timestep accumulator for platform-independent game loop timing.
  struct LoopTimer {
    std::chrono::steady_clock::time_point prev_time = std::chrono::steady_clock::now();
    float accumulator = 0.f;
    float target_dt = 0.f;
    float last_frame_dt = 0.f;
    uint64_t step_count =
        0; ///< Monotonic fixed-step counter. Prefer over elapsed_time for cooldowns and scripted events.

    /// @param fps Desired fixed-update rate; stored as seconds-per-step.
    explicit LoopTimer(float fps) noexcept : target_dt(1.f / fps) {}

    /// Update the target simulation rate at runtime (e.g., after loading config).
    void set_target_fps(float fps) noexcept {
      target_dt = 1.f / fps;
    }

    /// Sample wall clock and add elapsed time to accumulator. Call once per frame.
    /// Frame time is capped at 250 ms to prevent the simulation from running away
    /// after a hitch (debugger breakpoint, asset load, OS preemption).
    void tick() noexcept {
      auto now = std::chrono::steady_clock::now();
      const float raw_frame_time = std::chrono::duration<float>(now - prev_time).count();
      const float capped = std::min(raw_frame_time, 0.25f);
      last_frame_dt = capped;
      accumulator += capped;
      prev_time = now;
    }

    /// @return true while accumulated time covers another fixed step; false when exhausted.
    [[nodiscard]] bool step() noexcept {
      if (accumulator < target_dt) [[unlikely]]
        return false;
      accumulator -= target_dt;
      ++step_count;
      return true;
    }

    /** @brief Interpolation factor for render smoothing: accumulator / target_dt.
     *  @return Value in [0, 1) representing how far into the next fixed step we are.
     *  @pre target_dt must be > 0.
     */
    [[nodiscard]] float alpha() const noexcept {
      return std::min(accumulator / target_dt, 1.f);
    }
  };

} // namespace corundum::core::time
