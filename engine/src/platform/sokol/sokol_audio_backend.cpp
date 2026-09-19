// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "sokol_audio_backend.hpp"

#define STB_VORBIS_IMPLEMENTATION
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
#include <stb_vorbis.c> // NOLINT(bugprone-suspicious-include)
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <sokol_audio.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::platform::sokol {

  namespace {

    constexpr int k_target_rate = 44100;
    constexpr int k_target_channels = 2;

    /// sokol log levels: 0 = panic, 1 = error, 2 = warning, 3 = info. Surface failures
    /// only; routine warnings would be noise on the game's stderr.
    constexpr uint32_t k_max_reported_log_level = 1;

    // ── OGG loading (pure function, no instance state) ─────────────────────────

    struct LoadedClip {
      std::vector<float> samples; // interleaved stereo, [-1, 1]
      int num_frames{0};
    };

    struct VorbisCloser {
      void operator()(stb_vorbis *vorbis) const noexcept {
        if (vorbis != nullptr)
          stb_vorbis_close(vorbis);
      }
    };

    std::expected<LoadedClip, std::string> load_ogg(std::string_view path) {
      const std::string filename{path};
      int error = 0;
      const std::unique_ptr<stb_vorbis, VorbisCloser> vorbis{
          stb_vorbis_open_filename(filename.c_str(), &error, nullptr)};
      if (!vorbis)
        return std::unexpected(std::format("[audio] Failed to open OGG: {}", path));

      const auto info = stb_vorbis_get_info(vorbis.get());
      const int source_channels = info.channels;
      const int source_rate = static_cast<int>(info.sample_rate);
      if (source_channels < 1 || source_rate < 1)
        return std::unexpected(std::format("[audio] Unsupported OGG format: {}", path));

      // Decode the whole stream in chunks. stb_vorbis_stream_length_in_samples
      // reports 0 for a valid stream whose last-page granule position is absent,
      // so the reported length is never trusted.
      constexpr int k_chunk_frames = 4096;
      std::vector<float> source;
      std::vector<float> chunk(static_cast<std::size_t>(k_chunk_frames) * source_channels);
      for (;;) {
        const int decoded = stb_vorbis_get_samples_float_interleaved(vorbis.get(), source_channels, chunk.data(),
                                                                     static_cast<int>(chunk.size()));
        if (decoded <= 0)
          break;
        const auto count = static_cast<std::size_t>(decoded) * source_channels;
        source.insert(source.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count));
      }

      const int source_frames = static_cast<int>(source.size() / static_cast<std::size_t>(source_channels));
      if (source_frames == 0)
        return std::unexpected(std::format("[audio] Empty OGG file: {}", path));

      const float ratio = static_cast<float>(k_target_rate) / static_cast<float>(source_rate);
      const int dest_frames = std::max(1, static_cast<int>(static_cast<float>(source_frames) * ratio));

      LoadedClip clip;
      clip.num_frames = dest_frames;
      clip.samples.resize(static_cast<std::size_t>(dest_frames) * k_target_channels);

      for (int i = 0; i < dest_frames; ++i) {
        const float source_pos = static_cast<float>(i) / ratio;
        const int index = std::min(static_cast<int>(source_pos), source_frames - 1);
        const int next = std::min(index + 1, source_frames - 1);
        const float fraction = source_pos - static_cast<float>(index);

        for (int channel = 0; channel < k_target_channels; ++channel) {
          const int source_channel = std::min(channel, source_channels - 1);
          const float a = source[(static_cast<std::size_t>(index) * source_channels) + source_channel];
          const float b = source[(static_cast<std::size_t>(next) * source_channels) + source_channel];
          clip.samples[(static_cast<std::size_t>(i) * k_target_channels) + channel] = a + ((b - a) * fraction);
        }
      }

      return clip;
    }

    // ── Stream callback & mix state ───────────────────────────────────────────

    struct ActiveVoice {
      corundum::audio::SoundHandle clip_id;
      int64_t frame_cursor{0};
      float volume{1.0f};
      bool loop{false};
    };

    class SokolAudioBackend final : public corundum::audio::AudioBackend {
    public:
      SokolAudioBackend() {
        saudio_desc desc{};
        desc.stream_userdata_cb = &stream_cb;
        desc.user_data = this;
        desc.sample_rate = k_target_rate;
        desc.num_channels = k_target_channels;
        desc.logger.func = [](const char *tag, uint32_t level, uint32_t item_id, const char *msg, uint32_t line,
                              const char *file, void *) {
          if (level <= k_max_reported_log_level)
            std::println(stderr, "[{}] item={} ({}:{}) {}", tag ? tag : "saudio", item_id, file ? file : "?", line,
                         msg ? msg : "");
        };

        saudio_setup(&desc);
        valid_.store(saudio_isvalid());

        if (!valid_.load())
          std::println("[audio] WARN: saudio_setup() failed — audio disabled");
      }

      SokolAudioBackend(const SokolAudioBackend &) = delete;
      SokolAudioBackend &operator=(const SokolAudioBackend &) = delete;
      SokolAudioBackend(SokolAudioBackend &&) = delete;
      SokolAudioBackend &operator=(SokolAudioBackend &&) = delete;

      ~SokolAudioBackend() override {
        if (valid_.load())
          saudio_shutdown();
      }

      std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view path) override {
        if (!valid_.load())
          return std::unexpected("[audio] Audio disabled: saudio_setup() failed");

        auto clip_result = load_ogg(path);
        if (!clip_result)
          return std::unexpected(clip_result.error());

        const std::lock_guard<std::mutex> lock(mutex_);
        const auto handle = static_cast<corundum::audio::SoundHandle>(clips_.size() + 1);
        clips_.push_back(std::move(*clip_result));
        return handle;
      }

      void play(corundum::audio::SoundHandle handle, float volume, bool loop) override {
        if (!valid_.load())
          return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (!has_clip(handle))
          return;
        voices_.push_back(ActiveVoice{
            .clip_id = handle,
            .frame_cursor = 0,
            .volume = std::clamp(volume, 0.0f, 1.0f),
            .loop = loop,
        });
      }

      void set_master_volume(float volume) override {
        const std::lock_guard<std::mutex> lock(mutex_);
        master_volume_ = std::clamp(volume, 0.0f, 1.0f);
      }

    private:
      /// @pre The caller holds mutex_.
      [[nodiscard]] bool has_clip(corundum::audio::SoundHandle handle) const noexcept {
        return handle != 0 && handle <= clips_.size();
      }

      /// @pre has_clip(handle) is true.
      [[nodiscard]] const LoadedClip &clip_for(corundum::audio::SoundHandle handle) const noexcept {
        return clips_[static_cast<std::size_t>(handle) - 1];
      }

      /// @brief Mix @p num_frames of one voice into an interleaved output buffer.
      /// @pre The caller holds mutex_ and has_clip(voice.clip_id) is true.
      void mix_voice(ActiveVoice &voice, float *buffer, int num_frames, int num_channels) {
        const auto &clip = clip_for(voice.clip_id);
        if (clip.num_frames == 0)
          return;

        int offset = 0;
        while (offset < num_frames) {
          if (voice.frame_cursor >= clip.num_frames) {
            if (!voice.loop)
              break;
            voice.frame_cursor = 0;
          }

          const int remaining = clip.num_frames - static_cast<int>(voice.frame_cursor);
          const int batch = std::min(num_frames - offset, remaining);

          for (int i = 0; i < batch; ++i) {
            const int source_index = (static_cast<int>(voice.frame_cursor) + i) * k_target_channels;
            for (int channel = 0; channel < num_channels; ++channel) {
              const int mix_channel = std::min(channel, k_target_channels - 1);
              buffer[((offset + i) * num_channels) + channel] +=
                  clip.samples[static_cast<std::size_t>(source_index) + mix_channel] * voice.volume * master_volume_;
            }
          }

          voice.frame_cursor += batch;
          offset += batch;
        }
      }

      static void stream_cb(float *buffer, int num_frames, int num_channels, void *user_data) {
        auto *self = static_cast<SokolAudioBackend *>(user_data);
        if (self == nullptr || !self->valid_.load())
          return;

        std::memset(buffer, 0, static_cast<std::size_t>(num_frames) * num_channels * sizeof(float));

        // The lock is held for the whole mix, so play()/set_master_volume() on the
        // game thread block until the callback returns. Fine at the current call
        // frequency; a command queue would be the fix if that changes.
        const std::lock_guard<std::mutex> lock(self->mutex_);

        for (auto &voice : self->voices_) {
          if (self->has_clip(voice.clip_id))
            self->mix_voice(voice, buffer, num_frames, num_channels);
        }

        std::erase_if(self->voices_, [self](const ActiveVoice &voice) {
          if (voice.loop)
            return false;
          return !self->has_clip(voice.clip_id) || voice.frame_cursor >= self->clip_for(voice.clip_id).num_frames;
        });
      }

      std::mutex mutex_;
      std::vector<LoadedClip> clips_;
      std::vector<ActiveVoice> voices_;
      float master_volume_{1.0f};
      std::atomic<bool> valid_{false};
    };

  } // namespace

  // ── Factory ──────────────────────────────────────────────────────────────

  std::unique_ptr<corundum::audio::AudioBackend> make_sokol_audio_backend() {
    return std::make_unique<SokolAudioBackend>();
  }

} // namespace corundum::platform::sokol
