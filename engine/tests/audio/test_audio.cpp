// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/audio/audio_backend.hpp>
#include <corundum/audio/audio_system.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  /// Per-bus recording pre-filled with -1 so "never called" is distinguishable from a real 0.
  [[nodiscard]] std::array<float, corundum::audio::k_audio_bus_count> unset_bus_volumes() {
    std::array<float, corundum::audio::k_audio_bus_count> volumes{};
    volumes.fill(-1.f);
    return volumes;
  }

  /// Records load/play calls; hands out sequential handles starting at 1.
  class StubBackend final : public corundum::audio::AudioBackend {
  public:
    struct Played {
      corundum::audio::SoundHandle handle{};
      float volume{};
      bool loop{};
      corundum::audio::AudioBus bus{};
    };

    struct Stopped {
      corundum::audio::SoundInstanceId id{};
      float fade_out_seconds{};
    };

    struct VolumeChange {
      corundum::audio::SoundInstanceId id{};
      float volume{};
    };

    struct PitchChange {
      corundum::audio::SoundInstanceId id{};
      float pitch{};
    };

    std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view path) override {
      loaded_paths.emplace_back(path);
      return next_handle_++;
    }

    corundum::audio::SoundInstanceId play(corundum::audio::SoundHandle handle, float volume, bool loop,
                                          corundum::audio::AudioBus bus) override {
      if (should_fail_play)
        return {};
      played.push_back(Played{.handle = handle, .volume = volume, .loop = loop, .bus = bus});
      return corundum::audio::SoundInstanceId{.index = next_instance_++, .generation = 1};
    }

    void stop(corundum::audio::SoundInstanceId id, float fade_out_seconds) override {
      stopped.push_back(Stopped{.id = id, .fade_out_seconds = fade_out_seconds});
    }

    void set_volume(corundum::audio::SoundInstanceId id, float volume) override {
      volumes.push_back(VolumeChange{.id = id, .volume = volume});
    }

    void set_pitch(corundum::audio::SoundInstanceId id, float pitch) override {
      pitches.push_back(PitchChange{.id = id, .pitch = pitch});
    }

    void set_bus_volume(corundum::audio::AudioBus bus, float volume) override {
      bus_volumes[corundum::audio::bus_index(bus)] = volume;
    }

    void set_paused(bool paused) override { last_paused = paused; }

    std::expected<void, std::string> play_music(std::string_view path,
                                                const corundum::audio::MusicPlayback &playback) override {
      if (path == "bad.ogg")
        return std::unexpected(std::string{"cannot open bad.ogg"});
      music_played.emplace_back(path);
      last_playback = playback;
      return {};
    }

    void stop_music(float fade_out_seconds) override { music_stops.push_back(fade_out_seconds); }

    void update() override { ++updates; }

    bool last_paused = false;
    bool should_fail_play = false;
    std::vector<std::string> loaded_paths;
    std::vector<Played> played;
    std::vector<Stopped> stopped;
    std::vector<VolumeChange> volumes;
    std::vector<PitchChange> pitches;
    std::array<float, corundum::audio::k_audio_bus_count> bus_volumes = unset_bus_volumes();
    std::vector<std::string> music_played;
    corundum::audio::MusicPlayback last_playback{};
    std::vector<float> music_stops;
    int updates = 0;

  private:
    corundum::audio::SoundHandle next_handle_ = 1;
    std::uint32_t next_instance_ = 1;
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
  CHECK(stub->played[0].handle == 1);
}

TEST_CASE("AudioSystem: second play of the same name hits the cache (no second load)") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(sys.play_sound("coin", 0.5f, true).has_value());
  CHECK(stub->loaded_paths.size() == 1); // cached — loaded once
  REQUIRE(stub->played.size() == 2);
  CHECK(stub->played[1].loop); // loop flag forwarded
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
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Master)] == -1.f);

  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.set_master_volume(0.7f);
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Master)] == 0.7f);
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
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Music)] == -1.f);

  REQUIRE(sys.initialize("data/sounds").has_value());
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Music)] == 0.3f);
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

TEST_CASE("AudioSystem: play_sound returns a valid instance id and forwards bus, volume and loop") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  const auto id = sys.play_sound("coin", 0.4f, /*loop=*/true, corundum::audio::AudioBus::Ambience);
  REQUIRE(id.has_value());
  CHECK(id->is_valid());
  REQUIRE(stub->played.size() == 1);
  CHECK(stub->played[0].volume == 0.4f);
  CHECK(stub->played[0].loop);
  CHECK(stub->played[0].bus == corundum::audio::AudioBus::Ambience);
}

TEST_CASE("AudioSystem: stop_sound forwards the id and fade once ready") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  const auto id = sys.play_sound("coin");
  REQUIRE(id.has_value());
  sys.stop_sound(*id, 0.5f);

  REQUIRE(stub->stopped.size() == 1);
  CHECK(stub->stopped[0].id == *id);
  CHECK(stub->stopped[0].fade_out_seconds == 0.5f);
}

TEST_CASE("AudioSystem: set_sound_volume clamps and forwards the voice id") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  const auto id = sys.play_sound("coin");
  REQUIRE(id.has_value());
  sys.set_sound_volume(*id, 2.f);

  REQUIRE(stub->volumes.size() == 1);
  CHECK(stub->volumes[0].id == *id);
  CHECK(stub->volumes[0].volume == 1.f);
}

TEST_CASE("AudioSystem: set_sound_pitch forwards the voice id") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  const auto id = sys.play_sound("coin");
  REQUIRE(id.has_value());
  sys.set_sound_pitch(*id, 1.5f);

  REQUIRE(stub->pitches.size() == 1);
  CHECK(stub->pitches[0].id == *id);
  CHECK(stub->pitches[0].pitch == 1.5f);
}

TEST_CASE("AudioSystem: voice controls are swallowed before initialize") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  const corundum::audio::SoundInstanceId id{.index = 1, .generation = 1};

  sys.stop_sound(id);
  sys.set_sound_volume(id, 0.5f);
  sys.set_sound_pitch(id, 2.f);

  CHECK(stub->stopped.empty());
  CHECK(stub->volumes.empty());
  CHECK(stub->pitches.empty());
}

TEST_CASE("AudioSystem: voice controls with an invalid id are swallowed once ready") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  const corundum::audio::SoundInstanceId invalid{};

  sys.stop_sound(invalid);
  sys.set_sound_volume(invalid, 0.5f);
  sys.set_sound_pitch(invalid, 2.f);

  CHECK(stub->stopped.empty());
  CHECK(stub->volumes.empty());
  CHECK(stub->pitches.empty());
}

TEST_CASE("AudioSystem: play_sound reports an error when the backend cannot start the voice") {
  corundum::audio::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  stub->should_fail_play = true;

  const auto result = sys.play_sound("coin");
  REQUIRE_FALSE(result.has_value());
  CHECK_EQ(result.error(), "[audio] Could not start 'coin'");
}

TEST_CASE("AudioSystem: set_bus_volume clamps out-of-range volumes") {
  corundum::audio::AudioSystem sys;
  make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  sys.set_bus_volume(corundum::audio::AudioBus::Ambience, -0.5f);
  CHECK(sys.bus_volume(corundum::audio::AudioBus::Ambience) == 0.f);

  sys.set_bus_volume(corundum::audio::AudioBus::Ambience, 2.f);
  CHECK(sys.bus_volume(corundum::audio::AudioBus::Ambience) == 1.f);
}

TEST_CASE("AudioSystem: set_bus_volume is retained until initialize") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);

  sys.set_bus_volume(corundum::audio::AudioBus::Ambience, 0.4f);
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Ambience)] == -1.f);

  REQUIRE(sys.initialize("data/sounds").has_value());
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Ambience)] == 0.4f);
}

TEST_CASE("AudioSystem: master volume aliases its bus") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  sys.set_master_volume(0.3f);

  CHECK(sys.master_volume() == 0.3f);
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Master)] == 0.3f);
}

TEST_CASE("AudioSystem: music volume aliases its bus") {
  corundum::audio::AudioSystem sys;
  const StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  sys.set_music_volume(0.7f);

  CHECK(sys.music_volume() == 0.7f);
  CHECK(stub->bus_volumes[corundum::audio::bus_index(corundum::audio::AudioBus::Music)] == 0.7f);
}
