#include <corundum/audio/sys/audio_sys.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

namespace corundum::audio::sys {

  namespace {

    std::string resolve_path(const AudioState &state, std::string_view name) {
      auto it = state.catalog.find(std::string(name));
      if (it != state.catalog.end())
        return std::format("{}/{}", state.sounds_dir, it->second);
      return std::format("{}/{}.ogg", state.sounds_dir, name);
    }

  } // namespace

  std::expected<void, std::string> initialize(AudioState &state) {
    if (!state.backend)
      return std::unexpected("[audio] No audio backend set");
    state.cache.clear();
    state.initialized = true;
    return {};
  }

  void shutdown(AudioState &state) noexcept {
    if (!state.initialized)
      return;
    state.backend.reset();
    state.cache.clear();
    state.catalog.clear();
    state.initialized = false;
  }

  void load_catalog(AudioState &state, std::string_view catalog_path) noexcept {
    if (catalog_path.empty())
      return;

    std::ifstream file(catalog_path.data());
    if (!file) {
      std::println("[audio] WARN: Sound catalog not found: {}", catalog_path);
      return;
    }

    nlohmann::json j;
    try {
      j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::exception &e) {
      std::println("[audio] WARN: Malformed sound catalog {} - {}", catalog_path, e.what());
      return;
    }

    if (!j.is_object()) {
      std::println("[audio] WARN: Sound catalog must be a JSON object: {}", catalog_path);
      return;
    }

    state.catalog.clear();
    for (const auto &[key, value] : j.items()) {
      if (!value.is_string())
        continue;
      state.catalog.emplace(key, value.get<std::string>());
    }

    std::println("[audio] Loaded {} sound catalog entries", state.catalog.size());
  }

  std::expected<void, std::string> play_sound(AudioState &state, std::string_view name, float volume, bool loop) {
    if (!state.initialized || !state.backend)
      return std::unexpected("[audio] Audio system not initialised");

    const std::string key(name);

    // Check cache first.
    auto it = state.cache.find(key);
    if (it != state.cache.end()) {
      state.backend->play(it->second, volume, loop);
      return {};
    }

    // Resolve path through catalog or fallback.
    const std::string path = resolve_path(state, name);
    std::expected<audio::SoundHandle, std::string> handle_result = state.backend->load_sound(path);
    if (!handle_result)
      return std::unexpected(handle_result.error());

    state.cache.emplace(key, *handle_result);
    state.backend->play(*handle_result, volume, loop);
    return {};
  }

  void set_master_volume(AudioState &state, float volume) noexcept {
    if (!state.initialized || !state.backend)
      return;
    state.backend->set_master_volume(volume);
  }

} // namespace corundum::audio::sys
