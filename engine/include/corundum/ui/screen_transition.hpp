// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>

#include <cstdint>

namespace corundum::platform {
class Renderer;
} // namespace corundum::platform

namespace corundum::ui {

/** Which way a fade is currently moving. */
enum class TransitionPhase : std::uint8_t {
  /** Not fading; the overlay sits at its current alpha. */
  None,

  /** Alpha is rising toward 1 (into black). */
  FadingOut,

  /** Alpha is falling toward 0 (out of black). */
  FadingIn,
};

/** Value type driving a full-screen scene fade.
 *
 *  A value type: public per-call methods, no stored collaborators. The engine owns one and
 *  steps it from the fixed-timestep loop; the render sequence paints the overlay from alpha().
 *  Unlike a UI-screen transition, fade-out and fade-in are independent — a scene change starts
 *  with begin_fade_out(), applies its swap once at_black(), then calls begin_fade_in() to
 *  reveal the new scene. UI menus never touch this type; only scene/area changes do.
 */
class ScreenTransition {
public:
  /** Seconds for each direction of the fade — long enough to read as a deliberate transition. */
  static constexpr float k_fade_duration_seconds = 0.4f;

  /** Start fading toward black from the current alpha.
   *  @param seconds Time a full clear-to-black fade takes; defaults to k_fade_duration_seconds.
   */
  void begin_fade_out(float seconds = k_fade_duration_seconds) noexcept {
    duration_seconds_ = seconds;
    phase_ = TransitionPhase::FadingOut;
  }

  /** Jump to black and fade to clear — called once the scene swap is applied.
   *  @param seconds Time a full black-to-clear fade takes; defaults to k_fade_duration_seconds.
   */
  void begin_fade_in(float seconds = k_fade_duration_seconds) noexcept {
    duration_seconds_ = seconds;
    alpha_ = 1.f;
    phase_ = TransitionPhase::FadingIn;
  }

  /** Instant, un-faded state: clear and idle.
   *
   *  For tests and for a hard scene reset that must not animate.
   */
  void reset() noexcept {
    duration_seconds_ = k_fade_duration_seconds;
    phase_ = TransitionPhase::None;
    alpha_ = 0.f;
  }

  /** Advance by `dt` seconds toward the current phase's target, stopping at it.
   *  @note A no-op while phase() is None.
   */
  void update(float dt) noexcept {
    if (phase_ == TransitionPhase::None)
      return;
    // A non-positive duration is an instant snap; guarding here also keeps dt == 0 from
    // producing a NaN step when the duration is zero.
    if (duration_seconds_ <= 0.f) {
      alpha_ = (phase_ == TransitionPhase::FadingOut) ? 1.f : 0.f;
      phase_ = TransitionPhase::None;
      return;
    }
    const float step = dt / duration_seconds_;
    if (phase_ == TransitionPhase::FadingOut) {
      alpha_ += step;
      if (alpha_ >= 1.f) {
        alpha_ = 1.f;
        phase_ = TransitionPhase::None;
      }
      return;
    }
    alpha_ -= step;
    if (alpha_ <= 0.f) {
      alpha_ = 0.f;
      phase_ = TransitionPhase::None;
    }
  }

  /** Overlay opacity in [0, 1]; 0 draws nothing. */
  [[nodiscard]] float alpha() const noexcept { return alpha_; }

  /** Current phase. */
  [[nodiscard]] TransitionPhase phase() const noexcept { return phase_; }

  /** True while the overlay is fully black; a fade-out has finished. */
  [[nodiscard]] bool at_black() const noexcept { return alpha_ >= 1.f; }

  /** True while a fade is in progress. */
  [[nodiscard]] bool active() const noexcept { return phase_ != TransitionPhase::None; }

private:
  TransitionPhase phase_{TransitionPhase::None};

  float alpha_{0.f};

  /** Length of the fade in progress. */
  float duration_seconds_{k_fade_duration_seconds};
};

/** Fill the viewport with black at `transition`'s current alpha.
 *
 *  Emits one DrawRect when alpha() > 0 and nothing otherwise, so the idle cost is a compare.
 *
 *  @param r          Renderer; receives the overlay DrawRect.
 *  @param transition Supplies the opacity.
 *  @param viewport   Screen size in logical pixels.
 */
void render_screen_transition(platform::Renderer &r, const ScreenTransition &transition, core::math::Vec2 viewport);

} // namespace corundum::ui
