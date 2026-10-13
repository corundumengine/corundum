// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace corundum::platform::miniaudio {

  /** @brief Where a MiniaudioBackend sends its mix. */
  enum class MiniaudioOutput : std::uint8_t {
    Device,   ///< The default OS output device; miniaudio's audio thread pulls the mix.
    Headless, ///< No device: the mix advances only through render_frames(), for deterministic tests.
  };

  /** @brief AudioBackend over miniaudio's high-level engine (ma_engine).
   *
   *  Sound effects decode fully at load_sound() and play as copies of that data in an effects
   *  group. miniaudio decodes WAV, MP3 and FLAC natively and OGG Vorbis through stb_vorbis, and
   *  resamples every source to the engine rate. If the engine cannot start (no audio device), the
   *  backend stays usable: load_sound() reports an error and everything else is a no-op.
   *
   *  miniaudio.h (~96k lines) stays out of this header behind State; only miniaudio_backend.cpp and
   *  miniaudio_impl.cpp include it.
   *
   *  @note Game-thread only, like every AudioBackend.
   */
  class MiniaudioBackend final : public audio::AudioBackend {
  public:
    explicit MiniaudioBackend(MiniaudioOutput output);

    ~MiniaudioBackend() override;

    MiniaudioBackend(const MiniaudioBackend &) = delete;
    MiniaudioBackend &operator=(const MiniaudioBackend &) = delete;
    MiniaudioBackend(MiniaudioBackend &&) = delete;
    MiniaudioBackend &operator=(MiniaudioBackend &&) = delete;

    [[nodiscard]] std::expected<audio::SoundHandle, std::string> load_sound(std::string_view path) override;

    [[nodiscard]] audio::SoundInstanceId play(audio::SoundHandle handle, float volume, bool loop,
                                              audio::AudioBus bus) override;

    void stop(audio::SoundInstanceId id, float fade_out_seconds) override;

    void set_volume(audio::SoundInstanceId id, float volume) override;

    void set_pitch(audio::SoundInstanceId id, float pitch) override;

    void set_bus_volume(audio::AudioBus bus, float volume) override;

    void set_paused(bool paused) override;

    /** @brief Stream @p path in the music group, fading any current track out over
     *  @p playback.fade_in_seconds. The loop gap is scheduled on the engine clock by update(). */
    [[nodiscard]] std::expected<void, std::string> play_music(std::string_view path,
                                                              const audio::MusicPlayback &playback) override;

    void stop_music(float fade_out_seconds) override;

    void update() override;

    /** @brief Mix frames_out.size() / 2 interleaved stereo f32 frames into @p frames_out.
     *
     *  Fills silence when the engine failed to start.
     *  @pre Constructed with MiniaudioOutput::Headless.
     */
    void render_frames(std::span<float> frames_out);

  private:
    struct State;

    std::unique_ptr<State> state_;
  };

  /** @brief The production backend: a MiniaudioBackend on the default output device. */
  [[nodiscard]] std::unique_ptr<audio::AudioBackend> make_miniaudio_backend();

} // namespace corundum::platform::miniaudio
