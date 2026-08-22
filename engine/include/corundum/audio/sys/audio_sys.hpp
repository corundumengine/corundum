#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace corundum::audio::sys {

  /** @brief Owns the audio backend, sound cache, and catalog.
   *
   * @note Not thread-safe — all methods must be called from the game thread.
   *       The internal stream callback is behind a mutex.
   */
  class AudioSystem {
  public:
    AudioSystem() = default;

    /** @brief Adopt the platform audio backend. Must be called before initialize(). */
    void adopt_backend(std::unique_ptr<audio::AudioBackend> backend) noexcept {
      backend_ = std::move(backend);
    }

    /** @brief Initialise the audio system.
     *  @param[in] sounds_dir Base directory sound file paths are resolved against.
     *  @return ok on success, or an error if adopt_backend() was never called.
     *  @post On success, subsequent play_sound()/load_catalog() calls are active.
     */
    [[nodiscard]] std::expected<void, std::string> initialize(std::string sounds_dir);

    /** @brief Shut down the audio system and release all resources. Safe if never initialised. */
    void shutdown() noexcept;

    /** @brief Set the global master volume in [0.0, 1.0]. No-op if not initialised. */
    void set_master_volume(float volume) noexcept;

    /** @brief Load a JSON sound catalog mapping names to file paths.
     *
     *  Expects a flat JSON object: {"coin": "sfx/jingle_coin_01.ogg", ...}.
     *  Paths are relative to the sounds_dir passed to initialize(). If the file
     *  is missing or unparseable a warning is printed but the system remains
     *  functional.
     */
    void load_catalog(std::string_view catalog_path) noexcept;

    /** @brief Play a sound from a logical name, loading it on first use.
     *
     *  Resolves the file path as follows:
     *    1. If @p name is in the catalog → sounds_dir + catalog value.
     *    2. Otherwise → sounds_dir + name + ".ogg".
     *
     *  @return ok on success, or an error message if loading fails or the
     *          system is not initialised.
     */
    [[nodiscard]] std::expected<void, std::string> play_sound(std::string_view name, float volume = 1.0f,
                                                              bool loop = false);

  private:
    [[nodiscard]] std::string resolve_path(std::string_view name) const;

    std::unique_ptr<audio::AudioBackend> backend_;
    std::unordered_map<std::string, audio::SoundHandle> cache_;
    std::unordered_map<std::string, std::string> catalog_;
    std::string sounds_dir_;
    bool initialized_{false};
  };

} // namespace corundum::audio::sys
