// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>

#include <cstdint>

namespace corundum::platform {
  class Renderer;
} // namespace corundum::platform

namespace corundum::ui {

  /** @brief Which way a fade is currently moving. */
  enum class TransitionPhase : std::uint8_t {
    None,      ///< Not fading; the overlay sits at its current alpha.
    FadingOut, ///< Alpha is rising toward 1 (into black).
    FadingIn,  ///< Alpha is falling toward 0 (out of black).
  };

  /** @brief Value type driving a full-screen scene fade.
   *
   *  A value type: public per-call methods, no stored collaborators. The engine owns one and
   *  steps it from the fixed-timestep loop; the render sequence paints the overlay from alpha().
   *  Unlike a UI-screen transition, fade-out and fade-in are independent — a scene change starts
   *  with begin_fade_out(), applies its swap once at_black(), then calls begin_fade_in() to
   *  reveal the new scene. UI menus never touch this type; only scene/area changes do.
   */
  class ScreenTransition {
  public:
    /// Seconds for each direction of the fade — long enough to read as a deliberate transition.
    static constexpr float k_fade_duration_seconds = 0.4f;

    /** @brief Start fading toward black from the current alpha. */
    void begin_fade_out() noexcept {
      phase_ = TransitionPhase::FadingOut;
    }

    /** @brief Jump to black and fade to clear — called once the scene swap is applied. */
    void begin_fade_in() noexcept {
      alpha_ = 1.f;
      phase_ = TransitionPhase::FadingIn;
    }

    /** @brief Instant, un-faded state: clear and idle.
     *
     *  For tests and for a hard scene reset that must not animate.
     */
    void reset() noexcept {
      phase_ = TransitionPhase::None;
      alpha_ = 0.f;
    }

    /** @brief Advance by @p dt seconds toward the current phase's target, stopping at it.
     *  @note A no-op while phase() is None. */
    void update(float dt) noexcept {
      if (phase_ == TransitionPhase::None)
        return;
      const float step = dt / k_fade_duration_seconds;
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

    /** @brief Overlay opacity in [0, 1]; 0 draws nothing. */
    [[nodiscard]] float alpha() const noexcept {
      return alpha_;
    }

    /** @brief Current phase. */
    [[nodiscard]] TransitionPhase phase() const noexcept {
      return phase_;
    }

    /** @brief True while the overlay is fully black; a fade-out has finished. */
    [[nodiscard]] bool at_black() const noexcept {
      return alpha_ >= 1.f;
    }

    /** @brief True while a fade is in progress. */
    [[nodiscard]] bool active() const noexcept {
      return phase_ != TransitionPhase::None;
    }

  private:
    TransitionPhase phase_{TransitionPhase::None};

    float alpha_{0.f};
  };

  /** @brief Fill the viewport with black at @p transition's current alpha.
   *
   *  Emits one DrawRect when alpha() > 0 and nothing otherwise, so the idle cost is a compare.
   *
   *  @param r          Renderer; receives the overlay DrawRect.
   *  @param transition Supplies the opacity.
   *  @param viewport   Screen size in logical pixels.
   */
  void render_screen_transition(platform::Renderer &r, const ScreenTransition &transition, core::math::Vec2 viewport);

} // namespace corundum::ui
