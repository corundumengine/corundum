// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "platform/miniaudio/miniaudio_backend.hpp"
#include "temp_dir.hpp"

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

  /// Render @p frames frames in 441-frame (10 ms) chunks; returns the left channel.
  std::vector<float> render(MiniaudioBackend &backend, int frames) {
    constexpr int k_chunk = 441;
    std::vector<float> left;
    std::vector<float> chunk(static_cast<std::size_t>(k_chunk) * 2);
    for (int done = 0; done < frames; done += k_chunk) {
      backend.render_frames(chunk);
      for (int i = 0; i < k_chunk; ++i)
        left.push_back(chunk[static_cast<std::size_t>(i) * 2]);
    }
    return left;
  }

} // namespace

TEST_CASE("miniaudio backend: a WAV effect plays its samples, then silence") {
  const corundum::test::TempDir dir{"crpg_test_miniaudio_", "effect"};
  write_wav(dir / "blip.wav", 4410);
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound((dir / "blip.wav").string());
  REQUIRE(handle.has_value());

  backend.play(*handle, 1.f, /*loop=*/false);
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

  backend.set_master_volume(0.5f);
  backend.play(*handle, 1.f, /*loop=*/false);
  const std::vector<float> left = render(backend, 882);

  CHECK(left[100] == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("miniaudio backend: a missing file is a load error naming the path") {
  MiniaudioBackend backend{MiniaudioOutput::Headless};
  const auto handle = backend.load_sound("no/such/file.ogg");
  REQUIRE_FALSE(handle.has_value());
  CHECK(handle.error().find("no/such/file.ogg") != std::string::npos);
}
