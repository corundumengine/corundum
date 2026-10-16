// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "miniaudio_backend.hpp"
#include "content_vfs.hpp"
#include "core/warn_log.hpp"

#include <corundum/audio/audio_backend.hpp>
#include <corundum/core/content_source.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <miniaudio.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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
    /// One streamed music track and the gap to leave before its next loop.
    struct MusicTrack {
      SoundPtr sound;
      ma_uint64 loop_delay_frames{};
    };

    /// One mixer bus's miniaudio group. Master has no group: it is the engine volume.
    struct Bus {
      ma_sound_group group{};
      bool active{false};
    };

    /// One reusable playback voice; a null sound marks a free slot.
    struct Voice {
      SoundPtr sound;
      std::uint32_t generation{}; ///< 0 until first use; incremented on every play.
    };

    /// Declared before the engine: the resource manager keeps this pointer for the engine's life.
    ContentVfs vfs;
    ma_engine engine{};
    std::array<Bus, audio::k_audio_bus_count> buses{};
    MiniaudioOutput output{MiniaudioOutput::Device};
    bool is_ready{false};
    std::vector<SoundPtr> effect_sources; ///< Index = handle - 1; decoded at load.
    std::vector<Voice> effect_voices;     ///< Reusable playback slots, reclaimed in play()/update().

    std::optional<MusicTrack> music_track; ///< The one streamed track, when one is playing.
    std::vector<SoundPtr> fading_tracks;   ///< Replaced/stopped tracks still fading out.

    /// Seconds → milliseconds, the unit miniaudio's fade calls take.
    [[nodiscard]] static ma_uint64 seconds_to_milliseconds(float seconds) {
      return static_cast<ma_uint64>(std::lround(seconds * 1000.f));
    }

    /// The miniaudio group for @p bus, or nullptr for Master. miniaudio attaches a sound with no
    /// group straight to the engine endpoint, which is exactly what the Master bus means.
    [[nodiscard]] ma_sound_group *group_for(audio::AudioBus bus) {
      Bus &found = buses[audio::bus_index(bus)];
      return found.active ? &found.group : nullptr;
    }

    /// The live sound behind @p id, or nullptr when the id is stale or out of range.
    [[nodiscard]] ma_sound *find_voice(audio::SoundInstanceId id) {
      if (!id.is_valid() || id.index > effect_voices.size())
        return nullptr;
      const Voice &voice = effect_voices[id.index - 1];
      if (voice.sound == nullptr || voice.generation != id.generation)
        return nullptr;
      return voice.sound.get();
    }

    /// Mark every slot whose sound has finished as free, so play() can reuse it.
    void reclaim_finished_voices() {
      for (Voice &voice : effect_voices)
        if (voice.sound != nullptr && ma_sound_is_playing(voice.sound.get()) == MA_FALSE)
          voice.sound.reset();
    }

    /// Stop @p sound over @p seconds (0 stops at once), leaving it live until it goes silent.
    static void stop_sound_over(ma_sound *sound, float seconds) {
      if (seconds > 0.f)
        ma_sound_stop_with_fade_in_milliseconds(sound, seconds_to_milliseconds(seconds));
      else
        ma_sound_stop(sound);
    }

    /// Move @p sound into the fading set after starting its stop, so it is freed once silent.
    void fade_out_and_retire(SoundPtr sound, float seconds) {
      stop_sound_over(sound.get(), seconds);
      fading_tracks.push_back(std::move(sound));
    }

    /// Uninit every bus group that was created. Master has no group.
    void uninit_buses() {
      for (Bus &bus : buses)
        if (bus.active) {
          ma_sound_group_uninit(&bus.group);
          bus.active = false;
        }
    }
  };

  MiniaudioBackend::MiniaudioBackend(MiniaudioOutput output, const corundum::core::ContentSource &content)
      : state_(std::make_unique<State>()) {
    state_->output = output;

    ma_engine_config config = ma_engine_config_init();
    config.channels = 2;
    config.sampleRate = 44100;
    if (output == MiniaudioOutput::Headless)
      config.noDevice = MA_TRUE;
    state_->vfs = make_content_vfs(content);
    config.pResourceManagerVFS = &state_->vfs;

    const ma_result engine_result = ma_engine_init(&config, &state_->engine);
    if (engine_result != MA_SUCCESS) {
      detail::warn_log("[audio] WARN: miniaudio engine init failed: {} — audio disabled",
                       ma_result_description(engine_result));
      return;
    }

    // Master uses the engine volume; every other bus gets its own group.
    for (std::size_t i = 0; i < audio::k_audio_bus_count; ++i) {
      if (static_cast<audio::AudioBus>(i) == audio::AudioBus::Master)
        continue;
      State::Bus &bus = state_->buses[i];
      const ma_result group_result = ma_sound_group_init(&state_->engine, 0, nullptr, &bus.group);
      if (group_result != MA_SUCCESS) {
        detail::warn_log("[audio] WARN: miniaudio bus init failed: {} — audio disabled",
                         ma_result_description(group_result));
        state_->uninit_buses();
        ma_engine_uninit(&state_->engine);
        return;
      }
      bus.active = true;
    }

    state_->is_ready = true;
  }

  MiniaudioBackend::~MiniaudioBackend() {
    if (!state_->is_ready)
      return;
    // Voices and tracks reference the groups, so they must die first.
    state_->effect_voices.clear();
    state_->effect_sources.clear();
    state_->music_track.reset();
    state_->fading_tracks.clear();
    state_->uninit_buses();
    ma_engine_uninit(&state_->engine);
  }

  std::expected<audio::SoundHandle, std::string> MiniaudioBackend::load_sound(std::string_view path) {
    if (!state_->is_ready)
      return std::unexpected("[audio] Audio disabled: no audio engine");

    const std::string filename{path};
    auto sound = std::make_unique<ma_sound>();
    const ma_result result = ma_sound_init_from_file(&state_->engine, filename.c_str(),
                                                     MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                     state_->group_for(audio::AudioBus::Sound), nullptr, sound.get());
    if (result != MA_SUCCESS)
      return std::unexpected(std::format("[audio] Failed to load '{}': {}", path, ma_result_description(result)));

    state_->effect_sources.push_back(SoundPtr{sound.release()});
    return static_cast<audio::SoundHandle>(state_->effect_sources.size());
  }

  audio::SoundInstanceId MiniaudioBackend::play(audio::SoundHandle handle, float volume, bool loop,
                                                audio::AudioBus bus) {
    if (!state_->is_ready || handle == 0 || handle > state_->effect_sources.size())
      return {};

    state_->reclaim_finished_voices();

    // Reuse the first free slot so the voice table stays bounded by peak concurrency.
    const auto free_slot =
        std::ranges::find_if(state_->effect_voices, [](const State::Voice &voice) { return voice.sound == nullptr; });
    const std::size_t slot = static_cast<std::size_t>(free_slot - state_->effect_voices.begin());
    if (free_slot == state_->effect_voices.end())
      state_->effect_voices.push_back(State::Voice{});

    State::Voice &voice = state_->effect_voices[slot];
    ++voice.generation; // Every reuse invalidates ids handed out for the slot's previous voice.

    const ma_sound &source = *state_->effect_sources[static_cast<std::size_t>(handle) - 1];
    auto sound = std::make_unique<ma_sound>();
    const ma_result result = ma_sound_init_copy(&state_->engine, &source, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                                state_->group_for(bus), sound.get());
    if (result != MA_SUCCESS)
      return {};

    ma_sound_set_volume(sound.get(), std::clamp(volume, 0.f, 1.f));
    ma_sound_set_looping(sound.get(), static_cast<ma_bool32>(loop));
    ma_sound_start(sound.get());
    voice.sound = SoundPtr{sound.release()};
    return audio::SoundInstanceId{.index = static_cast<std::uint32_t>(slot) + 1u, .generation = voice.generation};
  }

  void MiniaudioBackend::stop(audio::SoundInstanceId id, float fade_out_seconds) {
    if (ma_sound *sound = state_->find_voice(id); sound != nullptr)
      State::stop_sound_over(sound, fade_out_seconds);
  }

  void MiniaudioBackend::set_volume(audio::SoundInstanceId id, float volume) {
    if (ma_sound *sound = state_->find_voice(id); sound != nullptr)
      ma_sound_set_volume(sound, std::clamp(volume, 0.f, 1.f));
  }

  void MiniaudioBackend::set_pitch(audio::SoundInstanceId id, float pitch) {
    if (ma_sound *sound = state_->find_voice(id); sound != nullptr)
      ma_sound_set_pitch(sound, pitch);
  }

  void MiniaudioBackend::set_bus_volume(audio::AudioBus bus, float volume) {
    if (!state_->is_ready)
      return;
    const float clamped = std::clamp(volume, 0.f, 1.f);
    if (bus == audio::AudioBus::Master) {
      ma_engine_set_volume(&state_->engine, clamped);
      return;
    }
    if (State::Bus &found = state_->buses[audio::bus_index(bus)]; found.active)
      ma_sound_group_set_volume(&found.group, clamped);
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

  std::expected<void, std::string> MiniaudioBackend::play_music(std::string_view path,
                                                                const audio::MusicPlayback &playback) {
    if (!state_->is_ready)
      return std::unexpected("[audio] Audio disabled: no audio engine");

    const std::string filename{path};
    auto sound = std::make_unique<ma_sound>();
    // Streams page in on miniaudio's job thread, which would make headless tests nondeterministic;
    // decode the whole file there. The loop and fade logic is identical either way.
    const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION |
                            (state_->output == MiniaudioOutput::Device ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE);
    const ma_result result = ma_sound_init_from_file(&state_->engine, filename.c_str(), flags,
                                                     state_->group_for(audio::AudioBus::Music), nullptr, sound.get());
    if (result != MA_SUCCESS)
      return std::unexpected(std::format("[audio] Failed to load '{}': {}", path, ma_result_description(result)));

    if (state_->music_track)
      state_->fade_out_and_retire(std::move(state_->music_track->sound), playback.fade_in_seconds);
    state_->music_track.reset();

    if (playback.fade_in_seconds > 0.f)
      ma_sound_set_fade_in_milliseconds(sound.get(), 0.f, 1.f,
                                        State::seconds_to_milliseconds(playback.fade_in_seconds));
    ma_sound_start(sound.get());
    state_->music_track = State::MusicTrack{
        .sound = SoundPtr{sound.release()},
        .loop_delay_frames = static_cast<ma_uint64>(std::llround(
            playback.loop_delay_seconds * static_cast<double>(ma_engine_get_sample_rate(&state_->engine)))),
    };
    return {};
  }

  void MiniaudioBackend::stop_music(float fade_out_seconds) {
    if (!state_->is_ready || !state_->music_track)
      return;
    state_->fade_out_and_retire(std::move(state_->music_track->sound), fade_out_seconds);
    state_->music_track.reset();
  }

  void MiniaudioBackend::update() {
    if (!state_->is_ready)
      return;
    if (state_->music_track && ma_sound_at_end(state_->music_track->sound.get()) != MA_FALSE) {
      ma_sound *sound = state_->music_track->sound.get();
      // ma_sound_start rewinds a sound at its end and clears the at-end flag, so this schedules
      // exactly one restart per loop. The engine clock freezes while the device is stopped (paused).
      ma_sound_set_start_time_in_pcm_frames(sound, ma_engine_get_time_in_pcm_frames(&state_->engine) +
                                                       state_->music_track->loop_delay_frames);
      ma_sound_start(sound);
    }
    std::erase_if(state_->fading_tracks, [](const SoundPtr &sound) { return !ma_sound_is_playing(sound.get()); });
    state_->reclaim_finished_voices();
  }

  void MiniaudioBackend::render_frames(std::span<float> frames_out) {
    assert(state_->output == MiniaudioOutput::Headless);
    if (!state_->is_ready) {
      std::ranges::fill(frames_out, 0.f);
      return;
    }
    ma_engine_read_pcm_frames(&state_->engine, frames_out.data(), frames_out.size() / 2, nullptr);
  }

  std::unique_ptr<audio::AudioBackend> make_miniaudio_backend(const corundum::core::ContentSource &content) {
    return std::make_unique<MiniaudioBackend>(MiniaudioOutput::Device, content);
  }

} // namespace corundum::platform::miniaudio
