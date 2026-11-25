// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/splash.hpp>

#include "gameplay/screens/screen_handlers.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/screen_transition.hpp>

#include <algorithm>

namespace corundum::gameplay::screens {

void splash_render(platform::Renderer &renderer, core::math::Vec2 viewport, const SplashState &state) {
  renderer.draw(platform::DrawRect{
      .position = {.x = 0.f, .y = 0.f},
      .size = viewport,
      .colour = core::math::Colour{.r = 0, .g = 0, .b = 0, .a = 255},
  });
  if (state.index >= state.images.size())
    return;
  const SplashImage &image = state.images[state.index];
  if (image.texture == platform::TextureId::Invalid || image.size.x <= 0.f || image.size.y <= 0.f)
    return;

  // Contain, not cover: a logo must never be cropped. Fit within a centered box smaller than the
  // viewport so a tightly cropped logo keeps a black margin.
  const float scale = std::min(viewport.x * k_splash_max_viewport_fraction / image.size.x,
                               viewport.y * k_splash_max_viewport_fraction / image.size.y);
  const core::math::Vec2 drawn_size{.x = image.size.x * scale, .y = image.size.y * scale};
  renderer.draw(platform::DrawSprite{
      .texture_id = image.texture,
      .position = {.x = (viewport.x - drawn_size.x) * 0.5f, .y = (viewport.y - drawn_size.y) * 0.5f},
      .source = {.x = 0, .y = 0, .width = static_cast<int>(image.size.x), .height = static_cast<int>(image.size.y)},
      .scale = {.x = scale, .y = scale},
  });
}

void update_splash(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  ui::ScreenTransition &fade = engine.screen_transition;
  SplashState &state = gameplay.splash_screen;
  const bool skip_requested = intent.activate || intent.back || intent.cursor_clicked;
  switch (fade.phase()) {
    case ui::TransitionPhase::FadingOut: return;
    case ui::TransitionPhase::FadingIn:
      if (skip_requested) {
        fade.begin_fade_out(k_splash_fade_seconds);
        return;
      }
      if (state.initial_delay_seconds > 0.f) {
        // screen_transition.update() ran before this step and lowered alpha; re-arm to full
        // black so the image stays hidden until the delay elapses.
        state.initial_delay_seconds = std::max(0.f, state.initial_delay_seconds - engine.timer.target_dt);
        fade.begin_fade_in(k_splash_fade_seconds);
      }
      return;
    case ui::TransitionPhase::None: break;
  }
  if (fade.at_black()) {
    gameplay.advance_splash();
    return;
  }
  state.shown_seconds += engine.timer.target_dt;
  if (skip_requested || state.shown_seconds >= k_splash_hold_seconds)
    fade.begin_fade_out(k_splash_fade_seconds);
}

void render_splash(const Engine &engine, const SplashState &state, platform::Renderer &renderer,
                   core::math::Vec2 viewport) {
  if (engine.scene.mode() != Splash)
    return;
  splash_render(renderer, viewport, state);
}

} // namespace corundum::gameplay::screens
