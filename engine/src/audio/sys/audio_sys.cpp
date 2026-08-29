#include <corundum/audio/sys/audio_sys.hpp>

#include <corundum/core/json_io.hpp>

#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <print>
#include <string>
#include <utility>

namespace corundum::audio::sys {

  std::string AudioSystem::resolve_path(std::string_view name) const {
    if (auto it = catalog_.find(name); it != catalog_.end())
      return std::format("{}/{}", sounds_dir_, it->second);
    return std::format("{}/{}.ogg", sounds_dir_, name);
  }

  std::expected<void, std::string> AudioSystem::initialize(std::string sounds_dir) {
    if (!backend_)
      return std::unexpected("[audio] No audio backend set");
    sounds_dir_ = std::move(sounds_dir);
    cache_.clear();
    initialized_ = true;
    return {};
  }

  void AudioSystem::shutdown() noexcept {
    if (!initialized_ && !backend_)
      return;
    backend_.reset();
    cache_.clear();
    catalog_.clear();
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
      std::println("[audio] WARN: Sound catalog unavailable ({}): {}", catalog_path, json_result.error());
      return;
    }
    const nlohmann::json &j = *json_result;

    if (!j.is_object()) {
      std::println("[audio] WARN: Sound catalog must be a JSON object: {}", catalog_path);
      return;
    }

    catalog_.clear();
    for (const auto &[key, value] : j.items()) {
      if (!value.is_string())
        continue;
      catalog_.emplace(key, value.get<std::string>());
    }

    std::println("[audio] Loaded {} sound catalog entries", catalog_.size());
  }

  std::expected<void, std::string> AudioSystem::play_sound(std::string_view name, float volume, bool loop) {
    if (!is_ready())
      return std::unexpected("[audio] Audio system not initialised");

    if (auto it = cache_.find(name); it != cache_.end()) {
      backend_->play(it->second, volume, loop);
      return {};
    }

    const std::string path = resolve_path(name);
    std::expected<audio::SoundHandle, std::string> handle_result = backend_->load_sound(path);
    if (!handle_result)
      return std::unexpected(handle_result.error());

    const auto [it, inserted] = cache_.try_emplace(std::string(name), *handle_result);
    backend_->play(it->second, volume, loop);
    return {};
  }

  void AudioSystem::set_master_volume(float volume) noexcept {
    if (!is_ready())
      return;
    backend_->set_master_volume(volume);
  }

} // namespace corundum::audio::sys
