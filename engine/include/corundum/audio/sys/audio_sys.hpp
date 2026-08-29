#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <flat_map>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::audio::sys {

  /** @brief Owns the audio backend, sound cache, and catalog.
   *
   * Lifecycle is RAII: the destructor releases the backend and clears caches.
   * `shutdown()` is the explicit form and is idempotent — call it from
   * well-defined lifecycle stages (e.g. `Engine::cleanup`) in addition to
   * letting the destructor run.
   *
   * @note Not thread-safe — all methods must be called from the game thread.
   *       The internal stream callback (in the sokol backend) is behind a mutex.
   */
  class AudioSystem {
  public:
    AudioSystem() = default;

    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    /// User-declared destructor suppresses the implicit move ctor; restore it so
    /// owning types (Engine) stay trivially movable.
    AudioSystem(AudioSystem &&other) noexcept = default;
    AudioSystem &operator=(AudioSystem &&other) noexcept = default;

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

    /** @brief Shut down the audio system and release all resources.
     *
     *  Idempotent — safe to call before initialize() or repeatedly.
     */
    void shutdown() noexcept;

    /** @brief RAII destructor — invokes shutdown() so a partially-initialised
     *  or in-use AudioSystem cannot leak its backend or cache. */
    ~AudioSystem() noexcept;

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
    /** @brief True once initialize() has succeeded and a backend is still set. */
    [[nodiscard]] bool is_ready() const noexcept {
      return initialized_ && backend_ != nullptr;
    }

    /** @brief Resolve a logical name to an absolute file path using the catalog
     *  (if present) or the sounds_dir + ".ogg" fallback. */
    [[nodiscard]] std::string resolve_path(std::string_view name) const;

    /** @brief Heterogeneous comparator: lets callers probe with std::string_view
     *  without first allocating a std::string for lookup. */
    struct StringLess {
      using is_transparent = void;

      [[nodiscard]] bool operator()(const std::string &lhs, const std::string &rhs) const noexcept {
        return lhs < rhs;
      }

      [[nodiscard]] bool operator()(std::string_view lhs, const std::string &rhs) const noexcept {
        return lhs < rhs;
      }

      [[nodiscard]] bool operator()(const std::string &lhs, std::string_view rhs) const noexcept {
        return lhs < rhs;
      }
    };

    std::unique_ptr<audio::AudioBackend> backend_;

    /** @brief SoundHandle cache keyed by logical name. */
    std::flat_map<std::string, audio::SoundHandle, StringLess> cache_;

    /** @brief Logical-name → relative-path catalog loaded from JSON. */
    std::flat_map<std::string, std::string, StringLess> catalog_;

    bool initialized_{false};

    std::string sounds_dir_;
  };

} // namespace corundum::audio::sys
