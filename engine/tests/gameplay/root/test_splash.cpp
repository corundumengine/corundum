// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/screens/splash.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/handle.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/texture_ref_counts.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  using corundum::test::RecordingRenderer;
  using corundum::ui::TransitionPhase;
  namespace platform = corundum::platform;
  namespace screens = corundum::gameplay::screens;

  /// Renderer that gives each path its own id and counts references like SokolRenderer. Any path
  /// containing "missing" fails to load, which is how these tests reach open_splash()'s skip path
  /// (the null renderer loads everything).
  class SplashRenderer final : public platform::Renderer {
  public:
    platform::TextureRefCounts refs;

    std::expected<uint32_t, std::string> load_texture(std::string_view path) override {
      if (path.contains("missing"))
        return std::unexpected(std::string{"no such file"});
      const auto [it, inserted] = ids_.try_emplace(std::string{path}, next_id_);
      if (inserted)
        ++next_id_;
      refs.acquire(it->second);
      return it->second;
    }

    [[nodiscard]] corundum::core::math::Vec2 texture_size(uint32_t /*texture_id*/) const override {
      return {.x = 64.f, .y = 32.f};
    }

    void release_texture(uint32_t texture_id) override {
      refs.release(texture_id);
    }

    std::expected<uint32_t, std::string> load_font(std::string_view /*path*/) override {
      return 1u;
    }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const platform::DrawSprite & /*cmd*/) override {}

    void draw(const platform::DrawText & /*cmd*/) override {}

    void draw(const platform::DrawRect & /*cmd*/) override {}

    void draw(const platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(uint32_t /*font_id*/, std::string_view /*text*/,
                                     uint32_t /*char_size*/) const override {
      return 0.f;
    }

    [[nodiscard]] platform::RendererStats stats() const override {
      return {};
    }

    /// The id load_texture() gave @p path, or 0 when it never loaded.
    [[nodiscard]] uint32_t id_of(std::string_view path) const {
      const auto it = ids_.find(std::string{path});
      return it == ids_.end() ? 0u : it->second;
    }

  private:
    std::unordered_map<std::string, uint32_t> ids_;

    uint32_t next_id_{1};
  };

  /// Adopt the null platform, then swap in a SplashRenderer the engine owns; returns it for
  /// assertions. Call before Engine::initialize().
  SplashRenderer &adopt_splash_renderer(corundum::Engine &engine) {
    adopt_platform(engine, 320, 240);
    auto *renderer = new SplashRenderer(); // NOLINT(cppcoreguidelines-owning-memory): the Handle below owns it.
    engine.adopt_renderer(platform::Handle<platform::Renderer>{
        renderer, platform::BackendDeleter<platform::Renderer>{&platform::null::detail::destroy_renderer}});
    return *renderer;
  }

  /// Fixed steps that comfortably cover any splash this file configures (about 6 s of steps).
  constexpr int k_step_budget = 60 * 10;

  corundum::core::GameConfig make_splash_config(bool show_title) {
    corundum::core::GameConfig cfg = make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
    cfg.show_title = show_title;
    cfg.game_id = "test_game";
    // The null renderer loads any path, so these need not exist on disk.
    cfg.paths.splash_images = {"studio.png", "engine.png"};
    return cfg;
  }

  /// One fixed step as run_fixed_steps() drives a step-owning screen: advance the fade, then
  /// dispatch the top screen with @p action (if any) pressed.
  void step(corundum::Engine &engine, std::optional<corundum::input::Action> action = std::nullopt) {
    engine.screen_transition.update(engine.timer.target_dt);
    corundum::input::InputState state{};
    if (action)
      state.pressed.set(static_cast<std::size_t>(*action));
    engine.input_state = state;
    const corundum::input::InputIntent intent =
        corundum::input::make_input_intent(engine.input_state, engine.input_mapper.last_device());
    static_cast<void>(engine.update_engine_screens(intent));
  }

  /// Step until the splash closes or the budget runs out; returns the steps taken.
  int step_until_splash_closes(corundum::Engine &engine) {
    int steps{};
    while (engine.scene.mode() == screens::Splash && steps < k_step_budget) {
      step(engine);
      ++steps;
    }
    return steps;
  }

  /// Steps that the hold alone spans at the engine's fixed rate.
  int hold_steps(const corundum::Engine &engine) {
    return static_cast<int>(screens::k_splash_hold_seconds / engine.timer.target_dt);
  }

} // namespace

TEST_CASE("splash: configured images open the splash at black, fading in") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_splash_config(/*show_title=*/true)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(engine.scene.ui.top() == screens::Splash);
  CHECK(gameplay.splash_screen.images.size() == 2);
  CHECK(gameplay.splash_screen.index == 0);
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingIn);
  CHECK(engine.screen_transition.at_black());
  CHECK(engine.render.mode == corundum::render::RenderMode::None);

  engine.cleanup();
}

TEST_CASE("splash: both images play in order, then the title fades in") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_splash_config(/*show_title=*/true)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  bool saw_second_image{false};
  int steps{};
  while (engine.scene.mode() == screens::Splash && steps < k_step_budget) {
    step(engine);
    saw_second_image = saw_second_image || gameplay.splash_screen.index == 1;
    ++steps;
  }

  CHECK(saw_second_image);
  CHECK(steps > 2 * hold_steps(engine)); // each image held for the full hold
  REQUIRE(engine.scene.ui.top() == screens::Title);
  CHECK(gameplay.splash_screen.images.empty());
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingIn);

  engine.cleanup();
}

TEST_CASE("splash: Activate during the hold skips to the next image") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_splash_config(/*show_title=*/true)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  for (int guard = 0; engine.screen_transition.active() && guard < k_step_budget; ++guard)
    step(engine);
  REQUIRE_FALSE(engine.screen_transition.active());

  step(engine, corundum::input::Action::Activate);
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingOut);

  int steps{};
  while (gameplay.splash_screen.index == 0 && steps < k_step_budget) {
    step(engine);
    ++steps;
  }
  CHECK(gameplay.splash_screen.index == 1);
  CHECK(steps < hold_steps(engine)); // only the fade-out ran, not the rest of the hold

  engine.cleanup();
}

TEST_CASE("splash: with show_title false the playthrough starts after the splash") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  REQUIRE(engine.initialize(make_splash_config(/*show_title=*/false)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(engine.scene.ui.top() == screens::Splash);
  CHECK(engine.render.mode == corundum::render::RenderMode::None); // no world under the splash

  step_until_splash_closes(engine);

  CHECK(engine.scene.ui.empty());
  CHECK(engine.render.mode != corundum::render::RenderMode::None);
  CHECK(engine.screen_transition.phase() == TransitionPhase::FadingIn);

  engine.cleanup();
}

TEST_CASE("splash: no configured images opens the title directly without a fade") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  corundum::core::GameConfig cfg = make_splash_config(/*show_title=*/true);
  cfg.paths.splash_images.clear();
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.scene.ui.top() == screens::Title);
  CHECK(engine.screen_transition.phase() == TransitionPhase::None);

  engine.cleanup();
}

TEST_CASE("splash: an image that fails to load is skipped and the other still plays") {
  corundum::Engine engine{};
  SplashRenderer &renderer = adopt_splash_renderer(engine);
  corundum::core::GameConfig cfg = make_splash_config(/*show_title=*/true);
  cfg.paths.splash_images = {"missing_studio.png", "engine.png"};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  REQUIRE(engine.scene.ui.top() == screens::Splash);
  REQUIRE(gameplay.splash_screen.images.size() == 1);
  CHECK(gameplay.splash_screen.images[0].texture == renderer.id_of("engine.png"));
  CHECK(gameplay.splash_screen.images[0].size.x == doctest::Approx(64.f));

  engine.cleanup();
}

TEST_CASE("splash: when every image fails to load the title opens directly") {
  corundum::Engine engine{};
  adopt_splash_renderer(engine);
  corundum::core::GameConfig cfg = make_splash_config(/*show_title=*/true);
  cfg.paths.splash_images = {"missing_studio.png", "missing_engine.png"};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.scene.ui.top() == screens::Title);
  CHECK(gameplay.splash_screen.images.empty());
  CHECK(engine.screen_transition.phase() == TransitionPhase::None);

  engine.cleanup();
}

TEST_CASE("splash: closing the splash gives back every texture reference") {
  corundum::Engine engine{};
  SplashRenderer &renderer = adopt_splash_renderer(engine);
  REQUIRE(engine.initialize(make_splash_config(/*show_title=*/true)).has_value());
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  const uint32_t studio = renderer.id_of("studio.png");
  const uint32_t engine_logo = renderer.id_of("engine.png");
  REQUIRE(studio != 0);
  REQUIRE(engine_logo != 0);
  CHECK(renderer.refs.count(studio) == 1);
  CHECK(renderer.refs.count(engine_logo) == 1);

  step_until_splash_closes(engine);

  REQUIRE(engine.scene.ui.top() == screens::Title);
  CHECK(renderer.refs.count(studio) == 0);
  CHECK(renderer.refs.count(engine_logo) == 0);

  engine.cleanup();
}

TEST_CASE("splash: the image fits a centered box k_splash_max_viewport_fraction of the viewport") {
  screens::SplashState state{};
  state.images.push_back(screens::SplashImage{.size = {.x = 32.f, .y = 64.f}, .texture = 7});
  RecordingRenderer r;
  screens::splash_render(r, {.x = 320.f, .y = 240.f}, state);

  REQUIRE(r.log.size() == 2);
  const auto *fill = std::get_if<corundum::platform::DrawRect>(&r.log[0]);
  REQUIRE(fill != nullptr);
  CHECK(fill->size.x == doctest::Approx(320.f));
  CHECK(fill->size.y == doctest::Approx(240.f));
  CHECK(fill->colour.r == 0);
  CHECK(fill->colour.a == 255);

  const auto *sprite = std::get_if<corundum::platform::DrawSprite>(&r.log[1]);
  REQUIRE(sprite != nullptr);
  CHECK(sprite->texture_id == 7);
  // The 0.75 box is 240 x 180; height-limited: 180 / 64 = 2.8125, so the 90 x 180 image centers at (115, 30).
  CHECK(sprite->scale.x == doctest::Approx(2.8125f));
  CHECK(sprite->scale.y == doctest::Approx(2.8125f));
  CHECK(sprite->position.x == doctest::Approx(115.f));
  CHECK(sprite->position.y == doctest::Approx(30.f));
}

TEST_CASE("splash: an image of unknown size draws only the black fill") {
  screens::SplashState state{};
  state.images.push_back(screens::SplashImage{.size = {}, .texture = 7});
  RecordingRenderer r;
  screens::splash_render(r, {.x = 320.f, .y = 240.f}, state);

  REQUIRE(r.log.size() == 1);
  CHECK(std::holds_alternative<corundum::platform::DrawRect>(r.log[0]));
}
