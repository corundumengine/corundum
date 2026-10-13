// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/audio/audio_backend.hpp>
#include <corundum/audio/audio_system.hpp>
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/json_io.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::audio {

  AudioSystem::AudioSystem() {
    bus_volumes_.fill(1.f);
  }

  std::string AudioSystem::resolve_path(std::string_view name) const {
    if (const auto it = catalog_.find(name); it != catalog_.end())
      return std::format("{}/{}", sounds_dir_, it->second);
    return std::format("{}/{}.ogg", sounds_dir_, name);
  }

  std::expected<void, std::string> AudioSystem::initialize(std::string sounds_dir) {
    if (!backend_)
      return std::unexpected("[audio] No audio backend set");
    sounds_dir_ = std::move(sounds_dir);
    cache_.clear();
    catalog_.clear();
    initialized_ = true;
    backend_->set_paused(paused_);
    for (std::size_t i = 0; i < k_audio_bus_count; ++i)
      backend_->set_bus_volume(static_cast<AudioBus>(i), bus_volumes_[i]);
    return {};
  }

  void AudioSystem::shutdown() noexcept {
    backend_.reset();
    cache_.clear();
    catalog_.clear();
    current_music_.clear();
    initialized_ = false;
  }

  AudioSystem::~AudioSystem() noexcept {
    shutdown();
  }

  void AudioSystem::load_catalog(std::string_view catalog_path) noexcept {
    if (catalog_path.empty())
      return;

    auto json_result = core::read_json(catalog_path);
    if (!json_result) {
      corundum::detail::warn_log("[audio] WARN: Sound catalog unavailable ({}): {}", catalog_path, json_result.error());
      return;
    }
    const nlohmann::json &j = *json_result;

    if (!j.is_object()) {
      corundum::detail::warn_log("[audio] WARN: Sound catalog must be a JSON object: {}", catalog_path);
      return;
    }

    catalog_.clear();
    for (const auto &[key, value] : j.items()) {
      if (!value.is_string())
        continue;
      catalog_.emplace(key, value.get<std::string>());
    }

    corundum::detail::info_log("[audio] Loaded {} sound catalog entries", catalog_.size());
  }

  std::expected<SoundInstanceId, std::string> AudioSystem::play_sound(std::string_view name, float volume, bool loop,
                                                                      AudioBus bus) {
    if (!is_ready())
      return std::unexpected("[audio] Audio system not initialised");

    const auto start_voice = [&](SoundHandle handle) -> std::expected<SoundInstanceId, std::string> {
      if (const SoundInstanceId id = backend_->play(handle, volume, loop, bus); id.is_valid())
        return id;
      return std::unexpected(std::format("[audio] Could not start '{}'", name));
    };

    if (const auto it = cache_.find(name); it != cache_.end())
      return start_voice(it->second);

    const std::string path = resolve_path(name);
    std::expected<SoundHandle, std::string> handle_result = backend_->load_sound(path);
    if (!handle_result)
      return std::unexpected(handle_result.error());

    const SoundHandle handle = cache_.try_emplace(std::string(name), *handle_result).first->second;
    return start_voice(handle);
  }

  void AudioSystem::stop_sound(SoundInstanceId id, float fade_out_seconds) noexcept {
    if (!is_ready() || !id.is_valid())
      return;
    backend_->stop(id, fade_out_seconds);
  }

  void AudioSystem::set_sound_volume(SoundInstanceId id, float volume) noexcept {
    if (!is_ready() || !id.is_valid())
      return;
    backend_->set_volume(id, std::clamp(volume, 0.f, 1.f));
  }

  void AudioSystem::set_sound_pitch(SoundInstanceId id, float pitch) noexcept {
    if (!is_ready() || !id.is_valid())
      return;
    backend_->set_pitch(id, pitch);
  }

  void AudioSystem::set_bus_volume(AudioBus bus, float volume) noexcept {
    const float clamped = std::clamp(volume, 0.f, 1.f);
    bus_volumes_[bus_index(bus)] = clamped;
    if (!is_ready())
      return;
    backend_->set_bus_volume(bus, clamped);
  }

  void AudioSystem::set_paused(bool paused) noexcept {
    paused_ = paused;
    if (!is_ready())
      return;
    backend_->set_paused(paused);
  }

  std::expected<void, std::string> AudioSystem::play_music(std::string_view path, const MusicPlayback &playback) {
    if (!is_ready())
      return std::unexpected("[audio] Audio system not initialised");
    if (path == current_music_)
      return {};
    if (std::expected<void, std::string> started = backend_->play_music(path, playback); !started)
      return started;
    current_music_ = path;
    return {};
  }

  void AudioSystem::stop_music(float fade_out_seconds) noexcept {
    if (current_music_.empty())
      return;
    current_music_.clear();
    if (is_ready())
      backend_->stop_music(fade_out_seconds);
  }

  void AudioSystem::update() noexcept {
    if (!is_ready())
      return;
    backend_->update();
  }

} // namespace corundum::audio
