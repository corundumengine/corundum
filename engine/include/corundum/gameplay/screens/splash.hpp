// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief Seconds each splash image holds at full opacity between its fade-in and fade-out. */
  constexpr float k_splash_hold_seconds = 2.f;

  /** @brief Largest share of the viewport's width and height a splash image may cover. Splash
   *  logos are usually tightly cropped, so fitting one to the whole viewport would run it edge to
   *  edge; the rest of the screen stays black. */
  constexpr float k_splash_max_viewport_fraction = 0.75f;

  /** @brief One loaded splash image. */
  struct SplashImage {
    core::math::Vec2 size{}; ///< Texture size in pixels; {0, 0} when the backend cannot report it (not drawn).

    std::uint32_t texture{}; ///< Renderer texture id; the splash holds one reference until it closes.
  };

  /** @brief Splash-screen state: the images loaded from GameConfig's splash_images in play order,
   *  the one showing, and how long it has held at full opacity.
   *
   *  Gameplay::open_splash() fills it at boot and Gameplay::advance_splash() empties it when the
   *  last image has faded out. The fades themselves live in Engine::screen_transition. */
  struct SplashState {
    std::vector<SplashImage> images{};

    std::size_t index{}; ///< The image showing; below images.size() while the splash is open.

    float shown_seconds{}; ///< Seconds the current image has held since its fade-in finished.
  };

  /** @brief Draw the splash: black over the whole viewport, then the current image scaled to fit
   *  within k_splash_max_viewport_fraction of the viewport and centered.
   *
   *  Pure render; invoked only while Splash is on top of the UI stack. The image is contained,
   *  never cropped, and the margin around it is black. An image of unknown size, or an
   *  index past the end, draws only the black fill. The engine draws the fade overlay afterward.
   *
   *  @param r        Renderer; emits the fill and at most one sprite.
   *  @param viewport Screen size in logical pixels.
   *  @param state    Loaded images and the index of the one showing.
   */
  void splash_render(platform::Renderer &r, core::math::Vec2 viewport, const SplashState &state);

} // namespace corundum::gameplay::screens
