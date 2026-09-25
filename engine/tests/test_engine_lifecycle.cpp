// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/audio/audio_backend.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/engine.hpp>
#include <corundum/platform/null/null_platform.hpp>
#include <corundum/platform/null/null_window.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/world/camera.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace {

  /// Build a GameConfig pointing at the headless fixtures bundled with the
  /// test target. sprites_dir ends at the parent directory so that
  /// `<sprites_dir>/characters/<sheet>.json` resolves correctly.
  corundum::core::GameConfig make_fixture_config(const fs::path &fixtures_root) {
    corundum::core::GameConfig cfg{};
    cfg.window_title = "lifecycle_test";
    cfg.win_w = 320.f;
    cfg.win_h = 240.f;
    cfg.paths.sprites_dir = (fixtures_root / "sprites").string();
    cfg.paths.tilemap_path = (fixtures_root / "tilemaps/lifecycle_test.json").string();
    cfg.paths.font_dir = (fixtures_root / "fonts").string();
    cfg.paths.game_font = "missing.ttf"; // NullRenderer ignores file existence
    cfg.paths.world_manifest_path.clear();
    cfg.paths.spawn_points_dir = (fixtures_root / "spawn_points").string();
    cfg.paths.portals_dir = (fixtures_root / "portals").string();
    cfg.paths.dialogue_dir.clear();
    cfg.paths.quests_dir.clear();
    cfg.paths.sounds_dir.clear();
    return cfg;
  }

  /// Adopt the NullPlatform into @p engine and reset engine state between tests.
  void adopt_platform(corundum::Engine &engine, unsigned w, unsigned h) {
    corundum::platform::null::NullPlatform platform{corundum::platform::null::make_null_platform(w, h)};
    corundum::platform::null::adopt_null_platform(engine, platform);
  }

  /// The concrete null window behind @p engine, so a test can script platform events.
  [[nodiscard]] corundum::platform::null::NullWindow *null_window(const corundum::Engine &engine) {
    return dynamic_cast<corundum::platform::null::NullWindow *>(engine.window.get());
  }

  /// Records every platform-event hook invocation for assertions.
  struct EventRecorder {
    int calls{0};
    corundum::platform::PlatformEvents seen{};

    void operator()(corundum::Engine & /*engine*/, const corundum::platform::PlatformEvents &events) {
      ++calls;
      seen = events;
    }
  };

  /// Audio backend double that records pause transitions for the focus tests.
  class RecordingAudioBackend final : public corundum::audio::AudioBackend {
  public:
    std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view /*path*/) override {
      return 1u;
    }

    void play(corundum::audio::SoundHandle /*handle*/, float /*volume*/, bool /*loop*/) override {}

    void set_master_volume(float /*volume*/) override {}

    void set_paused(bool paused) override {
      last_paused = paused;
    }

    bool last_paused{false};
  };

  /// Adopt a RecordingAudioBackend into @p engine's audio system; returns the
  /// raw pointer for assertions (owned by the system).
  [[nodiscard]] RecordingAudioBackend *adopt_recording_audio(corundum::Engine &engine) {
    auto backend = std::make_unique<RecordingAudioBackend>();
    RecordingAudioBackend *raw = backend.get();
    engine.audio.adopt_backend(std::move(backend));
    return raw;
  }

} // namespace

// ── 1. Precondition guard ────────────────────────────────────────────────────

TEST_CASE("lifecycle: initialize on a default-constructed Engine returns an error") {
  corundum::Engine engine{};
  std::expected<void, std::string> result{engine.initialize(corundum::core::GameConfig{})};
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("must be non-null") != std::string::npos);
}

// ── 2. Initialize succeeds with NullPlatform + fixture GameConfig ───────────

TEST_CASE("lifecycle: initialize succeeds with NullPlatform and a fixture GameConfig") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  REQUIRE(fs::is_directory(fixtures));

  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  std::expected<void, std::string> result{engine.initialize(std::move(cfg))};
  REQUIRE(result.has_value());
  CHECK(engine.window->is_open());
  engine.cleanup();
  CHECK_FALSE(engine.window->is_open());
}

// ── 3. Failure path runs cleanup (window closes, error returned) ────────────

TEST_CASE("lifecycle: initialize failure runs cleanup so the window is closed") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  cfg.paths.sprites_dir = (fixtures / "no_such_sprites_dir").string();

  std::expected<void, std::string> result{engine.initialize(std::move(cfg))};
  REQUIRE_FALSE(result.has_value());
  CHECK_FALSE(engine.window->is_open());
}

// ── 4. Double cleanup safe; run_frame/run_loop after cleanup return false ────

TEST_CASE("lifecycle: cleanup is idempotent and post-cleanup run_frame returns false") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  engine.cleanup();
  // Second cleanup must not crash.
  engine.cleanup();

  // Both run_frame and run_loop must exit immediately.
  CHECK_FALSE(engine.run_frame());
  engine.run_loop();
  CHECK(engine.quit_requested());
}

// ── 5. on_fixed_update can request_quit, run_loop exits cleanly ─────────────

TEST_CASE("lifecycle: on_fixed_update calling request_quit ends the main loop") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  int hook_calls{0};
  engine.on_fixed_update = [&hook_calls](corundum::Engine &e, float) {
    ++hook_calls;
    e.request_quit();
  };

  // Force at least one fixed step on the next tick.
  constexpr float k_steps_to_accumulate{2.f};
  engine.timer.accumulator = engine.timer.target_dt * k_steps_to_accumulate;

  engine.run_loop();

  CHECK(hook_calls >= 1);
  CHECK(engine.quit_requested());
  // Window should still be open after run_loop (single-close-path contract);
  // cleanup closes it.
  CHECK(engine.window->is_open());
  engine.cleanup();
  CHECK_FALSE(engine.window->is_open());
}

// ── 6. run_frame returns true across N steps, then false after request_quit ──

TEST_CASE("lifecycle: run_frame steps the simulation N times then returns false") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  constexpr float k_steps_to_accumulate{2.f};
  constexpr int k_frames_to_pump{5};

  // Pump frames; each should succeed.
  for (int i = 0; i < k_frames_to_pump; ++i) {
    engine.timer.accumulator = engine.timer.target_dt * k_steps_to_accumulate;
    CHECK(engine.run_frame());
  }

  // request_quit sets the quit flag; the next call must return false.
  engine.request_quit();
  CHECK_FALSE(engine.run_frame());

  engine.cleanup();
}

// ── 7. Single-map startup camera uses the map's true height, not its width ───

TEST_CASE("lifecycle: single-map startup camera frames the map with per-axis bounds") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  // lifecycle_test.json is 2x2 with a 32x16 diamond and tile_scale 2, so the world is
  // 3 * 32 * 2 = 192 px wide and 3 * 16 * 2 = 96 px tall — both smaller than the
  // 320x240 viewport, so each axis must center on its own true extent.
  constexpr float k_world_width{192.f};
  constexpr float k_world_height{96.f};
  CHECK(engine.scene.camera.x == doctest::Approx((k_world_width - 320.f) * 0.5f));
  CHECK(engine.scene.camera.y == doctest::Approx((k_world_height - 240.f) * 0.5f));

  engine.cleanup();
}

// ── 8. Focus loss pauses the simulation and audio ────────────────────────────

TEST_CASE("lifecycle: focus loss stops fixed steps and pauses audio") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  const RecordingAudioBackend *audio = adopt_recording_audio(engine);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  // Queue a fixed step so a running engine would consume one.
  engine.timer.accumulator = engine.timer.target_dt * 2.f;
  const std::uint64_t steps_before = engine.timer.step_count;

  null_window(engine)->scripted_events.focus_lost = true;
  CHECK(engine.run_frame());

  CHECK(engine.is_paused());
  CHECK(audio->last_paused);
  CHECK(engine.timer.step_count == steps_before); // simulation held still

  // Still paused, and still no catch-up, on later frames with time queued.
  engine.timer.accumulator = engine.timer.target_dt * 2.f;
  CHECK(engine.run_frame());
  CHECK(engine.timer.step_count == steps_before);

  engine.cleanup();
}

// ── 9. Focus gain resumes without replaying the paused interval ──────────────

TEST_CASE("lifecycle: focus gain resumes with no catch-up burst") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);
  const RecordingAudioBackend *audio = adopt_recording_audio(engine);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  null_window(engine)->scripted_events.focus_lost = true;
  REQUIRE(engine.run_frame());
  REQUIRE(engine.is_paused());

  // Simulate a long pause having queued a large backlog, then regain focus.
  engine.timer.accumulator = engine.timer.target_dt * 200.f;
  const std::uint64_t steps_before = engine.timer.step_count;

  null_window(engine)->scripted_events.focus_gained = true;
  CHECK(engine.run_frame());

  CHECK_FALSE(engine.is_paused());
  CHECK_FALSE(audio->last_paused);
  // The queued pause time was discarded rather than replayed as fixed steps.
  CHECK(engine.timer.step_count <= steps_before + 1);

  engine.cleanup();
}

// ── 10. quit_requested ends the frame; display changes reach the hook ─────────

TEST_CASE("lifecycle: quit_requested ends run_frame") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  null_window(engine)->scripted_events.quit_requested = true;
  CHECK_FALSE(engine.run_frame());
  CHECK(engine.quit_requested());

  engine.cleanup();
}

TEST_CASE("lifecycle: on_platform_event hook observes display changes") {
  corundum::Engine engine{};
  adopt_platform(engine, 320, 240);

  const fs::path fixtures{CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR};
  corundum::core::GameConfig cfg{make_fixture_config(fixtures)};
  REQUIRE(engine.initialize(std::move(cfg)).has_value());

  EventRecorder recorder;
  engine.on_platform_event = std::ref(recorder);

  null_window(engine)->scripted_events.display_changed = true;
  CHECK(engine.run_frame());
  CHECK(recorder.calls == 1);
  CHECK(recorder.seen.display_changed);

  // A frame with no OS events does not invoke the hook.
  CHECK(engine.run_frame());
  CHECK(recorder.calls == 1);

  engine.cleanup();
}
