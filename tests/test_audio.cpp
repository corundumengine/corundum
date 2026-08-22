#include <corundum/audio/sys/audio_sys.hpp>

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
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

    float last_master_volume = -1.f;
    std::vector<std::string> loaded_paths;
    std::vector<std::pair<corundum::audio::SoundHandle, bool>> played;

  private:
    corundum::audio::SoundHandle next_handle = 1;
  };

  /// Make an AudioSystem with an adopted stub backend; returns the raw stub
  /// pointer for assertions (owned by the system).
  StubBackend *make_system(corundum::audio::sys::AudioSystem &sys) {
    std::unique_ptr<StubBackend> stub = std::make_unique<StubBackend>();
    StubBackend *raw = stub.get();
    sys.adopt_backend(std::move(stub));
    return raw;
  }

} // namespace

TEST_CASE("AudioSystem: play_sound before initialize returns an error") {
  corundum::audio::sys::AudioSystem sys;
  make_system(sys);
  const auto result = sys.play_sound("coin");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] Audio system not initialised");
}

TEST_CASE("AudioSystem: initialize without a backend returns an error") {
  corundum::audio::sys::AudioSystem sys;
  const auto result = sys.initialize("data/sounds");
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "[audio] No audio backend set");
}

TEST_CASE("AudioSystem: play_sound resolves the .ogg fallback path against sounds_dir") {
  corundum::audio::sys::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(stub->loaded_paths.size() == 1);
  CHECK(stub->loaded_paths[0] == "data/sounds/coin.ogg");
  REQUIRE(stub->played.size() == 1);
  CHECK(stub->played[0].first == 1);
}

TEST_CASE("AudioSystem: second play of the same name hits the cache (no second load)") {
  corundum::audio::sys::AudioSystem sys;
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

  corundum::audio::sys::AudioSystem sys;
  StubBackend *stub = make_system(sys);
  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.load_catalog(catalog_path.string());

  REQUIRE(sys.play_sound("coin").has_value());
  REQUIRE(stub->loaded_paths.size() == 1);
  CHECK(stub->loaded_paths[0] == "data/sounds/sfx/jingle_coin_01.ogg");

  fs::remove(catalog_path);
}

TEST_CASE("AudioSystem: set_master_volume is a no-op before initialize, forwarded after") {
  corundum::audio::sys::AudioSystem sys;
  StubBackend *stub = make_system(sys);

  sys.set_master_volume(0.7f);
  CHECK(stub->last_master_volume == -1.f); // not initialised: swallowed

  REQUIRE(sys.initialize("data/sounds").has_value());
  sys.set_master_volume(0.7f);
  CHECK(stub->last_master_volume == 0.7f);
}
