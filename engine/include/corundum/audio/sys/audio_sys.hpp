#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace corundum::audio::sys {

  /** @brief All mutable audio state — pure data with no behaviour.
   *
   * Owns the AudioBackend and a filename-to-handle cache. Operated on by the
   * free functions in namespace corundum::audio::sys.
   *
   * @note Not thread-safe — all functions must be called from the game thread.
   *       The internal stream callback is behind a mutex.
   */
  struct AudioState {
    std::unique_ptr<audio::AudioBackend> backend;
    std::unordered_map<std::string, audio::SoundHandle> cache;
    std::unordered_map<std::string, std::string> catalog;
    std::string sounds_dir;
    bool initialized{false};
  };

  /** @brief Initialise the audio system.
   *
   *  The backend must already be set in state.backend (e.g. from make_engine)
   *  before calling this function.
   *
   *  @param[in,out] state  Uninitialised AudioState.
   *  @return ok on success.
   *  @post state.initialized is true.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(AudioState &state);

  /** @brief Shut down the audio system and release all resources.
   *  @param[in,out] state  Initialised AudioState.
   */
  void shutdown(AudioState &state) noexcept;

  /** @brief Set the global master volume.
   *  @param[in,out] state  Initialised AudioState.
   *  @param[in]     volume Volume level in [0.0, 1.0].
   */
  void set_master_volume(AudioState &state, float volume) noexcept;

  /** @brief Load a JSON sound catalog mapping names to file paths.
   *
   *  Expects a flat JSON object: {"coin": "sfx/jingle_coin_01.ogg", ...}.
   *  Paths are relative to sounds_dir. If the file is missing or unparseable
   *  a warning is printed but the system remains functional.
   *
   *  @param[in,out] state       Initialised AudioState.
   *  @param[in]     catalog_path Filesystem path to the catalog JSON.
   */
  void load_catalog(AudioState &state, std::string_view catalog_path) noexcept;

  /** @brief Play a sound from a logic name, loading it on first use.
   *
   *  Resolves the file path as follows:
   *    1. If @p name is in the catalog → sounds_dir + catalog value.
   *    2. Otherwise → sounds_dir + name + ".ogg".
   *  If the file has not been loaded yet it is loaded via the backend and
   *  cached for future calls.
   *
   *  @param[in,out] state  Initialised AudioState.
   *  @param[in]     name   Logical sound name (e.g. "coin" → catalog or "coin.ogg").
   *  @param[in]     volume Playback volume in [0.0, 1.0].
   *  @param[in]     loop   If true the sound loops indefinitely.
   *  @return ok on success, or an error message if loading fails.
   */
  [[nodiscard]] std::expected<void, std::string> play_sound(AudioState &state, std::string_view name,
                                                            float volume = 1.0f, bool loop = false);

} // namespace corundum::audio::sys
