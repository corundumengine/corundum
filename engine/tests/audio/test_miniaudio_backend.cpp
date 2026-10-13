// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "platform/miniaudio/miniaudio_backend.hpp"
#include "temp_dir.hpp"

#include <corundum/audio/audio_backend.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

  using corundum::platform::miniaudio::MiniaudioBackend;
  using corundum::platform::miniaudio::MiniaudioOutput;

  /// Engine rate the backend runs at; the WAVs below are written at it so nothing is resampled.
  constexpr int k_rate = 44100;

  /// 16384 / 32768: the f32 value a constant PCM16 sample of 16384 decodes to.
  constexpr float k_half = 0.5f;

  /// Write a 16-bit stereo k_rate WAV of @p frames frames, every sample 16384 (0.5 full scale).
  void write_wav(const fs::path &path, int frames) {
    const auto data_bytes = static_cast<std::uint32_t>(frames) * 4u;
    std::ofstream out(path, std::ios::binary);
    const auto put16 = [&out](std::uint32_t v) {
      out.put(static_cast<char>(v & 0xFFu));
      out.put(static_cast<char>((v >> 8u) & 0xFFu));
    };
    const auto put32 = [&put16](std::uint32_t v) {
      put16(v & 0xFFFFu);
      put16(v >> 16u);
    };
    out.write("RIFF", 4);
    put32(36u + data_bytes);
    out.write("WAVEfmt ", 8);
    put32(16u);
    put16(1u); // PCM
    put16(2u); // stereo
    put32(static_cast<std::uint32_t>(k_rate));
    put32(static_cast<std::uint32_t>(k_rate) * 4u);
    put16(4u);  // block align
    put16(16u); // bits per sample
    out.write("data", 4);
    put32(data_bytes);
    for (int i = 0; i < frames * 2; ++i)
      put16(16384u);
  }

  /// Render @p frames frames in 441-frame (10 ms) chunks, calling update() after each chunk the way
  /// the game loop does; returns the left channel.
  std::vector<float> render(MiniaudioBackend &backend, int frames) {
    constexpr int k_chunk = 441;
    std::vector<float> left;
    std::vector<float> chunk(static_cast<std::size_t>(k_chunk) * 2);
    for (int done = 0; done < frames; done += k_chunk) {
      backend.render_frames(chunk);
      for (int i = 0; i < k_chunk; ++i)
        left.push_back(chunk[static_cast<std::size_t>(i) * 2]);
      backend.update();
    }
    return left;
  }

  /// Start @p handle on the Sound bus at full volume, matching the interface's default.
  corundum::audio::SoundInstanceId play_effect(MiniaudioBackend &backend, corundum::audio::SoundHandle handle) {
    return backend.play(handle, 1.f, /*loop=*/false, corundum::audio::AudioBus::Sound);
  }

} // namespace

TEST_CASE("miniaudio backend: a WAV effect plays its samples, then silence") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "effect"};
  write_wav(dir / "blip.wav", 4410);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  const corundum::audio::SoundInstanceId id = play_effect(backend, *handle);
  CHECK(id.is_valid());
  const std::vector<float> left = render(backend, 8820);

  CHECK(left[100] == doctest::Approx(k_half).epsilon(0.01));
  CHECK(left[4000] == doctest::Approx(k_half).epsilon(0.01));
  CHECK(left[6000] == doctest::Approx(0.f));
}

TEST_CASE("miniaudio backend: master volume scales effects") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "master"};
  write_wav(dir / "blip.wav", 4410);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  backend.set_bus_volume(corundum::audio::AudioBus::Master, 0.5f);
  play_effect(backend, *handle);
  const std::vector<float> left = render(backend, 882);

  CHECK(left[100] == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("miniaudio backend: a missing file is a load error naming the path") {
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound("no/such/file.ogg");
  REQUIRE_FALSE(handle.has_value());
  CHECK(handle.error().find("no/such/file.ogg") != std::string::npos);
}

TEST_CASE("miniaudio backend: music restarts after loop_delay_seconds of silence") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "music_gap"};
  write_wav(dir / "track.wav", 4410); // 0.1 s
  MiniaudioBackend backend{MiniaudioOutput::Headless};

  REQUIRE(backend.play_music((dir / "track.wav").string(), {.fade_in_seconds = 0.f, .loop_delay_seconds = 0.2f})
              .has_value());
  const std::vector<float> left = render(backend, 22050); // 0.5 s

  CHECK(left[2000] == doctest::Approx(k_half).epsilon(0.01)); // first play
  CHECK(left[9000] == doctest::Approx(0.f));                  // inside the 0.2 s gap

  // The track ends at frame 4410; the restart is scheduled 8820 frames later, from the update()
  // that first sees the end. miniaudio raises its at-end flag only on the read that passes the
  // end, so that update() is one 441-frame chunk late, and the first mixed sample lands one frame
  // after the scheduled start.
  std::size_t restart = 5000;
  while (restart < left.size() && left[restart] == 0.f)
    ++restart;
  CHECK(restart >= 13230);
  CHECK(restart <= 13230 + 441 + 1);
  CHECK(left[restart + 1000] == doctest::Approx(k_half).epsilon(0.01)); // second play
}

TEST_CASE("miniaudio backend: stop_music fades the track to silence") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "music_stop"};
  write_wav(dir / "track.wav", 44100); // 1 s
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  REQUIRE(backend.play_music((dir / "track.wav").string(), {}).has_value());
  static_cast<void>(render(backend, 4410));

  backend.stop_music(0.1f);
  const std::vector<float> fade = render(backend, 8820);

  CHECK(fade[2205] > 0.05f); // halfway through the fade: quieter than full,
  CHECK(fade[2205] < 0.45f); // but not yet silent
  CHECK(fade[8000] == doctest::Approx(0.f));
}

TEST_CASE("miniaudio backend: a new track fades the previous one out over its fade-in") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "music_replace"};
  write_wav(dir / "first.wav", 44100);
  write_wav(dir / "second.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  REQUIRE(backend.play_music((dir / "first.wav").string(), {}).has_value());
  static_cast<void>(render(backend, 4410));

  REQUIRE(backend.play_music((dir / "second.wav").string(), {.fade_in_seconds = 0.1f, .loop_delay_seconds = 0.f})
              .has_value());
  const std::vector<float> left = render(backend, 8820);

  // Crossfade: the old track fading out plus the new one fading in sum to about full level.
  CHECK(left[2205] == doctest::Approx(k_half).epsilon(0.1));
  CHECK(left[8000] == doctest::Approx(k_half).epsilon(0.01)); // only the new track remains
}

TEST_CASE("miniaudio backend: music volume scales music but not effects") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "music_volume"};
  write_wav(dir / "track.wav", 44100);
  write_wav(dir / "blip.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  backend.set_bus_volume(corundum::audio::AudioBus::Music, 0.5f);

  REQUIRE(backend.play_music((dir / "track.wav").string(), {}).has_value());
  CHECK(render(backend, 882)[100] == doctest::Approx(0.25f).epsilon(0.01));
  backend.stop_music(0.f);
  static_cast<void>(render(backend, 441));

  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());
  play_effect(backend, *handle);
  CHECK(render(backend, 882)[100] == doctest::Approx(k_half).epsilon(0.01));
}

TEST_CASE("miniaudio backend: a bus volume scales only voices routed to it") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "bus_route"};
  write_wav(dir / "blip.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  backend.set_bus_volume(corundum::audio::AudioBus::Ambience, 0.5f);
  static_cast<void>(backend.play(*handle, 1.f, /*loop=*/false, corundum::audio::AudioBus::Ambience));
  CHECK(render(backend, 882)[100] == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("miniaudio backend: a voice routed to Master ignores the Sound bus volume") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "master_route"};
  write_wav(dir / "blip.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  backend.set_bus_volume(corundum::audio::AudioBus::Sound, 0.5f);
  static_cast<void>(backend.play(*handle, 1.f, /*loop=*/false, corundum::audio::AudioBus::Master));
  CHECK(render(backend, 882)[100] == doctest::Approx(k_half).epsilon(0.01));
}

TEST_CASE("miniaudio backend: set_volume adjusts a playing voice") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "voice_volume"};
  write_wav(dir / "blip.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  const corundum::audio::SoundInstanceId id = play_effect(backend, *handle);
  REQUIRE(id.is_valid());
  static_cast<void>(render(backend, 441));
  backend.set_volume(id, 0.5f);
  CHECK(render(backend, 882)[100] == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("miniaudio backend: stop silences a voice") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "voice_stop"};
  write_wav(dir / "blip.wav", 44100);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  const corundum::audio::SoundInstanceId id = play_effect(backend, *handle);
  REQUIRE(id.is_valid());
  static_cast<void>(render(backend, 441));
  backend.stop(id, 0.f);
  CHECK(render(backend, 882)[100] == doctest::Approx(0.f));
}

TEST_CASE("miniaudio backend: a stale id does not touch the voice that reused its slot") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "stale_id"};
  write_wav(dir / "blip.wav", 4410); // 0.1 s
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  const corundum::audio::SoundInstanceId first = play_effect(backend, *handle);
  REQUIRE(first.is_valid());
  static_cast<void>(render(backend, 8820)); // render past the end so update() reclaims the finished voice

  const corundum::audio::SoundInstanceId second = play_effect(backend, *handle);
  REQUIRE(second.is_valid());
  CHECK(second.index == first.index);           // same slot
  CHECK(second.generation != first.generation); // but a new voice

  backend.stop(first, 0.f);
  CHECK(render(backend, 882)[100] == doctest::Approx(k_half).epsilon(0.01));
}
