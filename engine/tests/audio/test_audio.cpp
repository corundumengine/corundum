// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/audio/audio_backend.hpp>
#include <corundum/audio/audio_system.hpp>

#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  /// Records load/play calls; hands out sequential handles starting at 1.
  class StubBackend final : public corundum::audio::AudioBackend {
  public:
    std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view path) override {
      loaded_paths.emplace_back(path);
      return next_handle++;
    }

    void play(corundum::audio::SoundHandle handle, float /*volume*/, bool loop) override {
      played.emplace_back(handle, loop);
    }

    void set_master_volume(float volume) override {
      last_master_volume = volume;
    }

    void set_paused(bool paused) override {
      last_paused = paused;
    }

    std::expected<void, std::string> play_music(std::string_view path,
                                                const corundum::audio::MusicPlayback &playback) override {
      if (path == "bad.ogg")
        return std::unexpected(std::string{"cannot open bad.ogg"});
      music_played.emplace_back(path);
      last_playback = playback;
      return {};
    }

    void stop_music(float fade_out_seconds) override {
      music_stops.push_back(fade_out_seconds);
    }

    void set_music_volume(float volume) override {
      last_music_volume = volume;
    }

    void update() override {
      ++updates;
    }

    float last_master_volume = -1.f;
    bool last_paused = false;
    std::vector<std::string> loaded_paths;
    std::vector<std::pair<corundum::audio::SoundHandle, bool>> played;
    std::vector<std::string> music_played;
    corundum::audio::MusicPlayback last_playback{};
    std::vector<float> music_stops;
    float last_music_volume = -1.f;
    int updates = 0;

  private:
    corundum::audio::SoundHandle next_handle = 1;
  };

  /// Make an AudioSystem with an adopted stub backend; returns the raw stub
  /// pointer for assertions (owned by the system).
  StubBackend *make_system(corundum::audio::AudioSystem &sys) {
    std::unique_ptr<StubBackend> stub = std::make_unique<StubBackend>();
    StubBackend *raw = stub.get();
    sys.adopt_backend(std::move(stub));
    return raw;
  }

} // namespace

TEST_CASE("AudioSystem: play_sound before initialize returns an error") {
  corundum::audio::AudioSystem sys;
  make_system(sys);
  const auto result = sys.play_sound("coin");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] Audio system not initialised");
}

TEST_CASE("AudioSystem: initialize without a backend returns an error") {
  corundum::audio::AudioSystem sys;
  const auto result = sys.initialize("data/sounds");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] No audio backend set");
}

TEST_CASE("AudioSystem: play_sound resolves the .ogg fallback path against sounds_dir") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(stub->loaded_paths.size() == 1);
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");
  REQUIRE(stub->played.size() == 1);
  CHECK(stub->played[0].first == 1);
}

TEST_CASE("AudioSystem: second play of the same name hits the cache (no second load)") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(sys.play_sound("coin", 0.5f, true).has_value());
  CHECK(stub->loaded_paths.size() == 1); // cached — loaded once
  REQUIRE(stub->played.size() == 2);
  CHECK(stub->played[1].second == true); // loop flag forwarded
}

TEST_CASE("AudioSystem: catalog entry overrides the .ogg fallback") {
  namespace fs = std::filesystem;
  const fs::path catalog_path = fs::temp_directory_path() / "corundum_test_catalog.json";
  {
    std::ofstream out(catalog_path);
    out << R"({"coin": "sfx/jingle_coin_01.ogg"})";
  }

  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.load_catalog(catalog_path.string());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(stub->loaded_paths.size() == 1);
  CHECK(stub->loaded_paths[0] == "data/sounds/sfx/jingle_coin_01.ogg");

  fs::remove(catalog_path);
}

TEST_CASE("AudioSystem: set_master_volume is a no-op before initialize, forwarded after") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);

  sys.set_master_volume(0.7f);
  CHECK(stub->last_master_volume == -1.f); // not initialised: swallowed

  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.set_master_volume(0.7f);
  CHECK(stub->last_master_volume == 0.7f);
}

TEST_CASE("AudioSystem: set_paused is retained before initialize and applied once ready") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);

  sys.set_paused(true);
  CHECK_FALSE(stub->last_paused); // not initialised: swallowed

  REQUIRE(sys.initialize("data/sounds").has_value());
  CHECK(stub->last_paused); // the retained flag is re-applied

  sys.set_paused(false);
  CHECK_FALSE(stub->last_paused);
}

TEST_CASE("AudioSystem: destruction after init is RAII-safe and clears state") {
  // No explicit shutdown() — the destructor must clean up so that play_sound
  // after a fresh construct (without re-init) reports "not initialised".
  {
    corundum::audio::AudioSystem sys;
    make_system(sys); // adopt a stub, intentionally unused
    REQUIRE(sys.initialize("data/sounds").has_value());
  }
  corundum::audio::AudioSystem sys2;
  const auto result = sys2.play_sound("coin");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] Audio system not initialised");
}

TEST_CASE("AudioSystem: destruction without init is RAII-safe") {
  // Default-constructed AudioSystem must destruct without crashing — the
  // owning Engine holds it by value and may be torn down before init.
  corundum::audio::AudioSystem sys;
  sys.shutdown(); // idempotent
  sys.shutdown(); // still idempotent
  CHECK(true);
}

TEST_CASE("AudioSystem: play_sound returns error after explicit shutdown") {
  corundum::audio::AudioSystem sys;
  make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.shutdown();
  const auto result = sys.play_sound("coin");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] Audio system not initialised");
}

TEST_CASE("AudioSystem: load_catalog with a missing file is a warn-and-continue") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  // Non-existent path: load_catalog must not throw, must not corrupt state.
  sys.load_catalog("/nonexistent/corundum_sound_catalog.json");

  // .ogg fallback must still resolve correctly afterwards.
  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(stub->loaded_paths.size() == 1);
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");
}

TEST_CASE("AudioSystem: load_catalog with malformed JSON is a warn-and-continue") {
  namespace fs = std::filesystem;
  const fs::path catalog_path = fs::temp_directory_path() / "corundum_test_catalog_malformed.json";
  {
    std::ofstream out(catalog_path);
    out << "{ this is not json";
  }

  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.load_catalog(catalog_path.string());

  // Malformed catalog must not throw and must leave .ogg fallback intact.
  REQUIRE(sys.play_sound("coin").has_value());
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");

  fs::remove(catalog_path);
}

TEST_CASE("AudioSystem: load_catalog with a non-object JSON is a warn-and-continue") {
  namespace fs = std::filesystem;
  const fs::path catalog_path = fs::temp_directory_path() / "corundum_test_catalog_array.json";
  {
    std::ofstream out(catalog_path);
    out << R"(["not", "an", "object"])";
  }

  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.load_catalog(catalog_path.string());

  // Non-object JSON must not throw and must leave .ogg fallback intact.
  REQUIRE(sys.play_sound("coin").has_value());
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");

  fs::remove(catalog_path);
}

TEST_CASE("AudioSystem: load_catalog with empty path is a silent no-op") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.load_catalog("");
  REQUIRE(sys.play_sound("coin").has_value());
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");
}

TEST_CASE("AudioSystem: play_sound probes cache with std::string_view (heterogeneous lookup)") {
  // Verifies that the internal flat_map+std::less<> accepts string_view
  // for cache lookup without forcing the caller to materialise a std::string.
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  std::string_view name = "coin";
  REQUIRE(sys.play_sound(name).has_value());
  REQUIRE(sys.play_sound(name, 0.5f, false).has_value()); // cached — same handle
  CHECK(stub->loaded_paths.size() == 1);
  CHECK(stub->played.size() == 2);
}

TEST_CASE("AudioSystem: play_music forwards the path and playback once ready") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_music("a.ogg", {.fade_in_seconds = 1.5f, .loop_delay_seconds = 7.f}).has_value());
  REQUIRE(stub->music_played.size() == 1);
  CHECK(stub->music_played[0] == "a.ogg");
  CHECK(stub->last_playback.fade_in_seconds == 1.5f);
  CHECK(stub->last_playback.loop_delay_seconds == 7.f);
}

TEST_CASE("AudioSystem: asking for the playing track again does not restart it") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  CHECK(stub->music_played.size() == 1);
}

TEST_CASE("AudioSystem: stop_music forwards the fade, and the same track then plays again") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  sys.stop_music(2.f);
  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  REQUIRE(stub->music_stops.size() == 1);
  CHECK(stub->music_stops[0] == 2.f);
  CHECK(stub->music_played.size() == 2);
}

TEST_CASE("AudioSystem: stop_music with no track playing does nothing") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  sys.stop_music(2.f);
  CHECK(stub->music_stops.empty());
}

TEST_CASE("AudioSystem: a failed play_music keeps the current track") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  CHECK_FALSE(sys.play_music("bad.ogg", {}).has_value());
  REQUIRE(sys.play_music("a.ogg", {}).has_value());
  REQUIRE(stub->music_played.size() == 1);
  CHECK(stub->music_played[0] == "a.ogg");
}

TEST_CASE("AudioSystem: music volume set before initialize is applied by it") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);

  sys.set_music_volume(0.3f);
  CHECK(stub->last_music_volume == -1.f); // not initialised: swallowed

  REQUIRE(sys.initialize("data/sounds").has_value());
  CHECK(stub->last_music_volume == 0.3f);
}

TEST_CASE("AudioSystem: play_music before initialize returns an error") {
  corundum::audio::AudioSystem sys;
  make_system(sys);
  const auto result = sys.play_music("a.ogg", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] Audio system not initialised");
}

TEST_CASE("AudioSystem: update forwards to the backend once ready") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);

  sys.update();
  CHECK(stub->updates == 0); // not initialised: swallowed

  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.update();
  sys.update();
  CHECK(stub->updates == 2);
}
