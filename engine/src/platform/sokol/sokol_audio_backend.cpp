#include "sokol_audio_backend.hpp"

#define STB_VORBIS_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
#include <stb_vorbis.c>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <sokol_audio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <expected>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::platform::glfw {

  namespace {

    constexpr int k_target_rate = 44100;
    constexpr int k_target_channels = 2;

    // ── OGG loading (pure function, no instance state) ─────────────────────────

    struct LoadedClip {
      std::vector<float> samples; // interleaved stereo, [-1, 1]
      int num_frames{0};
    };

    std::expected<LoadedClip, std::string> load_ogg(std::string_view path) {
      int error = 0;
      stb_vorbis *vorbis = stb_vorbis_open_filename(path.data(), &error, nullptr);
      if (!vorbis)
        return std::unexpected(std::format("[audio] Failed to open OGG: {}", path));

      const auto info = stb_vorbis_get_info(vorbis);
      const int src_channels = info.channels;
      const int src_rate = info.sample_rate;
      const int total_samples = stb_vorbis_stream_length_in_samples(vorbis);

      if (total_samples <= 0) {
        stb_vorbis_close(vorbis);
        return std::unexpected(std::format("[audio] Empty OGG file: {}", path));
      }

      std::vector<float> src(static_cast<std::size_t>(total_samples) * src_channels);
      const int decoded =
          stb_vorbis_get_samples_float_interleaved(vorbis, src_channels, src.data(), static_cast<int>(src.size()));
      stb_vorbis_close(vorbis);

      if (decoded <= 0)
        return std::unexpected(std::format("[audio] Failed to decode OGG: {}", path));

      const float ratio = static_cast<float>(k_target_rate) / static_cast<float>(src_rate);
      const int dst_frames = std::max(1, static_cast<int>(static_cast<float>(decoded) * ratio));

      LoadedClip clip;
      clip.num_frames = dst_frames;
      clip.samples.resize(static_cast<std::size_t>(dst_frames) * k_target_channels);

      for (int i = 0; i < dst_frames; ++i) {
        const float src_pos = static_cast<float>(i) / ratio;
        const int idx = std::min(static_cast<int>(src_pos), decoded - 1);
        const int next = std::min(idx + 1, decoded - 1);
        const float frac = src_pos - static_cast<float>(idx);

        for (int ch = 0; ch < k_target_channels; ++ch) {
          const int sc = std::min(ch, src_channels - 1);
          const float a = src[static_cast<std::size_t>(idx) * src_channels + sc];
          const float b = src[static_cast<std::size_t>(next) * src_channels + sc];
          clip.samples[static_cast<std::size_t>(i) * k_target_channels + ch] = a + (b - a) * frac;
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

        saudio_setup(&desc);
        valid_ = saudio_isvalid();

        if (!valid_)
          std::println("[audio] WARN: saudio_setup() failed — audio disabled");
      }

      ~SokolAudioBackend() override {
        if (valid_)
          saudio_shutdown();
      }

      std::expected<corundum::audio::SoundHandle, std::string> load_sound(std::string_view path) override {
        auto clip_result = load_ogg(path);
        if (!clip_result)
          return std::unexpected(clip_result.error());

        const std::lock_guard<std::mutex> lock(mutex_);
        const auto handle = static_cast<corundum::audio::SoundHandle>(clips_.size());
        clips_.push_back(std::move(*clip_result));
        return handle;
      }

      void play(corundum::audio::SoundHandle handle, float volume, bool loop) override {
        if (!valid_)
          return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (handle >= clips_.size())
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
      static void stream_cb(float *buffer, int num_frames, int num_channels, void *user_data) {
        auto *self = static_cast<SokolAudioBackend *>(user_data);
        if (!self || !self->valid_)
          return;

        std::memset(buffer, 0, static_cast<std::size_t>(num_frames) * num_channels * sizeof(float));

        const std::lock_guard<std::mutex> lock(self->mutex_);

        for (auto &voice : self->voices_) {
          if (voice.clip_id >= self->clips_.size())
            continue;

          auto &clip = self->clips_[voice.clip_id];
          if (clip.num_frames == 0)
            continue;

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
              const int src_idx = (static_cast<int>(voice.frame_cursor) + i) * k_target_channels;
              for (int ch = 0; ch < num_channels; ++ch) {
                const int mix_ch = std::min(ch, k_target_channels - 1);
                buffer[(offset + i) * num_channels + ch] +=
                    clip.samples[static_cast<std::size_t>(src_idx) + mix_ch] * voice.volume * self->master_volume_;
              }
            }

            voice.frame_cursor += batch;
            offset += batch;
          }
        }

        std::erase_if(self->voices_, [self](const ActiveVoice &v) {
          return !v.loop && (v.clip_id >= self->clips_.size() || v.frame_cursor >= self->clips_[v.clip_id].num_frames);
        });
      }

      std::mutex mutex_;
      std::vector<LoadedClip> clips_;
      std::vector<ActiveVoice> voices_;
      float master_volume_{1.0f};
      bool valid_{false};
    };

  } // namespace

  // ── Factory ──────────────────────────────────────────────────────────────

  std::unique_ptr<corundum::audio::AudioBackend> make_sokol_audio_backend() {
    return std::make_unique<SokolAudioBackend>();
  }

} // namespace corundum::platform::glfw
