// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/audio/audio_backend.hpp>
#include <corundum/audio/audio_system.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include "world_transition_fixtures.hpp"

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  using corundum::test::adopt_platform;
  using corundum::test::make_world_config;
  namespace screens = corundum::gameplay::screens;

  /// AudioBackend double that records what the gameplay framework asks the music system to do.
  class MusicRecorder final : public corundum::audio::AudioBackend {
  public:
    std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view /*path*/) override {
      return 1u;
    }

    corundum::audio::SoundInstanceId play(corundum::audio::SoundHandle /*handle*/, float /*volume*/, bool /*loop*/,
                                          corundum::audio::AudioBus /*bus*/) override {
      return {};
    }

    void stop(corundum::audio::SoundInstanceId /*id*/, float /*fade_out_seconds*/) override {}

    void set_volume(corundum::audio::SoundInstanceId /*id*/, float /*volume*/) override {}

    void set_pitch(corundum::audio::SoundInstanceId /*id*/, float /*pitch*/) override {}

    void set_bus_volume(corundum::audio::AudioBus /*bus*/, float /*volume*/) override {}

    void set_paused(bool /*paused*/) override {}

    std::expected<void, std::string> play_music(std::string_view path,
                                                const corundum::audio::MusicPlayback &playback) override {
      played_tracks.emplace_back(path);
      playbacks.push_back(playback);
      return {};
    }

    void stop_music(float fade_out_seconds) override { stop_fades.push_back(fade_out_seconds); }

    std::vector<std::string> played_tracks;
    std::vector<corundum::audio::MusicPlayback> playbacks;
    std::vector<float> stop_fades;
  };

  /// Adopt the null platform and a MusicRecorder, initialise the engine with the title-music
  /// config, and return the recorder for assertions. Call before constructing Gameplay.
  MusicRecorder &boot(corundum::Engine &engine, bool show_title,
                      corundum::core::TitleMusicConfig music = {.loop_delay_seconds = 7.f, .track = "music/title.ogg"},
                      std::vector<std::string> splash_images = {}) {
    adopt_platform(engine, 320, 240);
    auto recorder = std::make_unique<MusicRecorder>();
    MusicRecorder *raw = recorder.get();
    engine.audio.adopt_backend(std::move(recorder));

    corundum::core::GameConfig cfg = make_world_config(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR);
    cfg.game_id = "test_game";
    cfg.title_music = std::move(music);
    cfg.show_title = show_title;
    cfg.paths.splash_images = std::move(splash_images);
    REQUIRE(engine.initialize(std::move(cfg)).has_value());
    return *raw;
  }

  /// Fixed steps that comfortably cover any splash or load these tests drive.
  constexpr int k_step_budget = 60 * 10;

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

} // namespace

TEST_CASE("title music: starts at boot under the splash") {
  corundum::Engine engine{};
  const MusicRecorder &recorder =
      boot(engine, /*show_title=*/true, {.loop_delay_seconds = 7.f, .track = "music/title.ogg"}, {"a.png", "b.png"});
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(engine.scene.ui.top() == screens::Splash);
  REQUIRE(recorder.played_tracks.size() == 1);
  CHECK(recorder.played_tracks[0] == "music/title.ogg");
  REQUIRE(recorder.playbacks.size() == 1);
  CHECK(recorder.playbacks[0].loop_delay_seconds == doctest::Approx(7.f));
  CHECK(recorder.playbacks[0].fade_in_seconds > 0.f);

  engine.cleanup();
}

TEST_CASE("title music: skipping every splash image never interrupts it") {
  corundum::Engine engine{};
  const MusicRecorder &recorder =
      boot(engine, /*show_title=*/true, {.loop_delay_seconds = 7.f, .track = "music/title.ogg"}, {"a.png", "b.png"});
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  int steps{};
  while (engine.scene.mode() == screens::Splash && steps < k_step_budget) {
    step(engine, corundum::input::Action::Activate);
    ++steps;
  }
  REQUIRE(engine.scene.ui.top() == screens::Title);

  CHECK(recorder.played_tracks.size() == 1);
  CHECK(recorder.stop_fades.empty());

  engine.cleanup();
}

TEST_CASE("title music: New Game fades it out") {
  corundum::Engine engine{};
  const MusicRecorder &recorder = boot(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(engine.scene.ui.top() == screens::Title);

  REQUIRE(gameplay.new_game().has_value());

  REQUIRE(recorder.stop_fades.size() == 1);
  CHECK(recorder.stop_fades[0] > 0.f);

  engine.cleanup();
}

TEST_CASE("title music: returning to the title starts it again") {
  corundum::Engine engine{};
  const MusicRecorder &recorder = boot(engine, /*show_title=*/true);
  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};
  REQUIRE(gameplay.new_game().has_value());

  gameplay.return_to_title();

  CHECK(recorder.played_tracks.size() == 2);

  engine.cleanup();
}

TEST_CASE("title music: with show_title false it starts at boot and fades when the world loads") {
  corundum::Engine engine{};
  const MusicRecorder &recorder = boot(engine, /*show_title=*/false);

  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(recorder.played_tracks.size() == 1);
  CHECK(recorder.stop_fades.size() == 1);

  engine.cleanup();
}

TEST_CASE("title music: none configured plays nothing") {
  corundum::Engine engine{};
  const MusicRecorder &recorder = boot(engine, /*show_title=*/true, {});

  // NOLINTNEXTLINE(misc-const-correctness): constructing Gameplay registers the framework hooks.
  corundum::gameplay::Gameplay gameplay{engine};

  CHECK(recorder.played_tracks.empty());
  CHECK(recorder.stop_fades.empty());

  engine.cleanup();
}
