// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "miniaudio_backend.hpp"
#include "core/warn_log.hpp"

#include <corundum/audio/audio_backend.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <miniaudio.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::platform::miniaudio {

  namespace {

    /// Deletes an ma_sound after uninitialising it. An ma_sound must never move once
    /// ma_sound_init_* has written into it, so it is always owned behind a pointer.
    struct SoundDeleter {
      void operator()(ma_sound *sound) const noexcept {
        ma_sound_uninit(sound);
        std::default_delete<ma_sound>{}(sound);
      }
    };

    using SoundPtr = std::unique_ptr<ma_sound, SoundDeleter>;

  } // namespace

  struct MiniaudioBackend::State {
    ma_engine engine{};
    ma_sound_group effects{};
    MiniaudioOutput output{MiniaudioOutput::Device};
    bool is_ready{false};
    std::vector<SoundPtr> effect_sources; ///< Index = handle - 1; decoded at load.
    std::vector<SoundPtr> effect_voices;  ///< Live playback copies, reclaimed in play().
  };

  MiniaudioBackend::MiniaudioBackend(MiniaudioOutput output) : state_(std::make_unique<State>()) {
    state_->output = output;

    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 44100;
    if (output == MiniaudioOutput::Headless)
      config.noDevice = MA_TRUE;

    const ma_result engine_result = ma_engine_init(&config, &state_->engine);
    if (engine_result != MA_SUCCESS) {
      detail::warn_log("[audio] WARN: miniaudio engine init failed: {} — audio disabled",
                       ma_result_description(engine_result));
      return;
    }

    const ma_result group_result = ma_sound_group_init(&state_->engine, 0, nullptr, &state_->effects);
    if (group_result != MA_SUCCESS) {
      detail::warn_log("[audio] WARN: miniaudio engine init failed: {} — audio disabled",
                       ma_result_description(group_result));
      ma_engine_uninit(&state_->engine);
      return;
    }

    state_->is_ready = true;
  }

  MiniaudioBackend::~MiniaudioBackend() {
    if (!state_->is_ready)
      return;
    // Voices and sources reference the groups, so they must die first.
    state_->effect_voices.clear();
    state_->effect_sources.clear();
    ma_sound_group_uninit(&state_->effects);
    ma_engine_uninit(&state_->engine);
  }

  std::expected<audio::SoundHandle, std::string> MiniaudioBackend::load_sound(std::string_view path) {
    if (!state_->is_ready)
      return std::unexpected("[audio] Audio disabled: no audio engine");

    const std::string filename{path};
    auto sound = std::make_unique<ma_sound>();
    const ma_result result = ma_sound_init_from_file(&state_->engine, filename.c_str(),
                                                     MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                     &state_->effects, nullptr, sound.get());
    if (result != MA_SUCCESS)
      return std::unexpected(std::format("[audio] Failed to load '{}': {}", path, ma_result_description(result)));

    state_->effect_sources.push_back(SoundPtr{sound.release()});
    return static_cast<audio::SoundHandle>(state_->effect_sources.size());
  }

  void MiniaudioBackend::play(audio::SoundHandle handle, float volume, bool loop) {
    if (!state_->is_ready || handle == 0 || handle > state_->effect_sources.size())
      return;

    std::erase_if(state_->effect_voices, [](const SoundPtr &voice) { return !ma_sound_is_playing(voice.get()); });

    const ma_sound &source = *state_->effect_sources[static_cast<std::size_t>(handle) - 1];
    auto voice = std::make_unique<ma_sound>();
    const ma_result result =
        ma_sound_init_copy(&state_->engine, &source, MA_SOUND_FLAG_NO_SPATIALIZATION, &state_->effects, voice.get());
    if (result != MA_SUCCESS)
      return;

    ma_sound_set_volume(voice.get(), std::clamp(volume, 0.f, 1.f));
    ma_sound_set_looping(voice.get(), static_cast<ma_bool32>(loop));
    ma_sound_start(voice.get());
    state_->effect_voices.push_back(SoundPtr{voice.release()});
  }

  void MiniaudioBackend::set_master_volume(float volume) {
    if (state_->is_ready)
      ma_engine_set_volume(&state_->engine, std::clamp(volume, 0.f, 1.f));
  }

  void MiniaudioBackend::set_paused(bool paused) {
    if (!state_->is_ready || state_->output != MiniaudioOutput::Device)
      return;
    // Stopping the device freezes the engine clock and holds every voice at its position, so
    // resuming continues from the same sample rather than jumping ahead by the paused interval.
    if (paused)
      ma_engine_stop(&state_->engine);
    else
      ma_engine_start(&state_->engine);
  }

  void MiniaudioBackend::render_frames(std::span<float> frames_out) {
    assert(state_->output == MiniaudioOutput::Headless);
    if (!state_->is_ready) {
      std::ranges::fill(frames_out, 0.f);
      return;
    }
    ma_engine_read_pcm_frames(&state_->engine, frames_out.data(), frames_out.size() / 2, nullptr);
  }

  std::unique_ptr<audio::AudioBackend> make_miniaudio_backend() {
    return std::make_unique<MiniaudioBackend>(MiniaudioOutput::Device);
  }

} // namespace corundum::platform::miniaudio
