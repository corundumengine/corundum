// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <array>
#include <cstddef>
#include <expected>
#include <flat_map>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::core {
  class ContentSource;
}

namespace corundum::audio {

  /** @brief Owns the audio backend, sound cache, and catalog.
   *
   * Lifecycle is RAII: the destructor releases the backend and clears caches.
   * `shutdown()` is the explicit form and is idempotent — call it from
   * well-defined lifecycle stages (e.g. `Engine::cleanup`) in addition to
   * letting the destructor run.
   *
   * @note Not thread-safe — all methods must be called from the game thread.
   *       The backend owns its own mixing thread; no caller-visible locking is
   *       involved.
   */
  class AudioSystem {
  public:
    AudioSystem();

    AudioSystem(const AudioSystem &) = delete;
    AudioSystem &operator=(const AudioSystem &) = delete;

    /// User-declared destructor suppresses the implicit move ctor; restore it so
    /// owning types (Engine) stay trivially movable.
    AudioSystem(AudioSystem &&other) noexcept = default;
    AudioSystem &operator=(AudioSystem &&other) noexcept = default;

    /** @brief Adopt the platform audio backend. Must be called before initialize(). */
    void adopt_backend(std::unique_ptr<audio::AudioBackend> backend) noexcept { backend_ = std::move(backend); }

    /** @brief Initialise the audio system.
     *  @param[in] sounds_dir Base directory sound file paths are resolved against.
     *  @return ok on success, or an error if adopt_backend() was never called.
     *  @post On success, subsequent play_sound()/load_catalog() calls are active.
     *  @note Re-initialising clears the sound cache and catalog, so a stale
     *        catalog from a previous sounds_dir cannot leak across calls.
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

    /** @brief Set @p bus's volume in [0.0, 1.0]. Retained before initialize() and applied by it. */
    void set_bus_volume(AudioBus bus, float volume) noexcept;

    /** @brief The last volume set for @p bus, in [0.0, 1.0]. Defaults to 1.0. */
    [[nodiscard]] float bus_volume(AudioBus bus) const noexcept { return bus_volumes_[bus_index(bus)]; }

    /** @brief Set the master volume in [0.0, 1.0]. Retained before initialize() and applied by it. */
    void set_master_volume(float volume) noexcept { set_bus_volume(AudioBus::Master, volume); }

    /** @brief The last master volume set, in [0.0, 1.0]. Defaults to 1.0. */
    [[nodiscard]] float master_volume() const noexcept { return bus_volume(AudioBus::Master); }

    /** @brief Set the music volume in [0.0, 1.0]. Retained before initialize() and applied by it. */
    void set_music_volume(float volume) noexcept { set_bus_volume(AudioBus::Music, volume); }

    /** @brief The last music volume set, in [0.0, 1.0]. Defaults to 1.0. */
    [[nodiscard]] float music_volume() const noexcept { return bus_volume(AudioBus::Music); }

    /** @brief Pause or resume all audio output. No-op if not initialised.
     *
     *  The paused flag is retained, so a later initialize() re-applies it to the
     *  backend rather than silently resuming.
     */
    void set_paused(bool paused) noexcept;

    /** @brief Load a JSON sound catalog mapping names to file paths.
     *
     *  Expects a flat JSON object: {"coin": "sfx/jingle_coin_01.ogg", ...}.
     *  Paths are relative to the sounds_dir passed to initialize(). If the file
     *  is missing or unparseable a warning is printed but the system remains
     *  functional.
     */
    void load_catalog(const core::ContentSource &content, std::string_view catalog_path) noexcept;

    /** @brief Play a sound from a logical name on @p bus, loading it on first use.
     *
     *  Resolves the file path as follows:
     *    1. If @p name is in the catalog → sounds_dir + catalog value.
     *    2. Otherwise → sounds_dir + name + ".ogg".
     *
     *  @return The started voice's id, or an error message if loading fails or
     *          the system is not initialised.
     */
    [[nodiscard]] std::expected<SoundInstanceId, std::string>
    play_sound(std::string_view name, float volume = 1.0f, bool loop = false, AudioBus bus = AudioBus::Sound);

    /** @brief Stop voice @p id, fading over @p fade_out_seconds (0 stops at once). No-op until ready. */
    void stop_sound(SoundInstanceId id, float fade_out_seconds = 0.f) noexcept;

    /** @brief Set voice @p id's volume in [0.0, 1.0]. No-op until ready. */
    void set_sound_volume(SoundInstanceId id, float volume) noexcept;

    /** @brief Set voice @p id's pitch multiplier (1.0 leaves it unchanged). No-op until ready.
     *  @note A non-positive @p pitch is ignored. */
    void set_sound_pitch(SoundInstanceId id, float pitch) noexcept;

    /** @brief Play @p path (a project-relative file path, not a catalog name) as the music track.
     *
     *  Asking for the track that is already playing does nothing, so callers state the music they
     *  want without tracking whether it is on.
     *  @return ok, or why the track could not start; an error when not initialised.
     */
    [[nodiscard]] std::expected<void, std::string> play_music(std::string_view path, const MusicPlayback &playback);

    /** @brief Fade the current track out over @p fade_out_seconds. No-op when no track is playing. */
    void stop_music(float fade_out_seconds) noexcept;

    /** @brief Per-frame housekeeping (music loop restarts); call once per frame. No-op until ready. */
    void update() noexcept;

  private:
    /** @brief True once initialize() has succeeded and a backend is still set. */
    [[nodiscard]] bool is_ready() const noexcept { return initialized_ && backend_ != nullptr; }

    /** @brief Resolve a logical name to an absolute file path using the catalog
     *  (if present) or the sounds_dir + ".ogg" fallback. */
    [[nodiscard]] std::string resolve_path(std::string_view name) const;

    std::unique_ptr<audio::AudioBackend> backend_;

    /** @brief SoundHandle cache keyed by logical name. std::less<> gives
     *  heterogeneous lookup, so string_view probes avoid a std::string alloc. */
    std::flat_map<std::string, audio::SoundHandle, std::less<>> cache_;

    /** @brief Logical-name → relative-path catalog loaded from JSON. */
    std::flat_map<std::string, std::string, std::less<>> catalog_;

    std::string current_music_; ///< Path of the playing track; empty when none.

    bool initialized_{false};

    bool paused_{false};

    /** @brief Per-bus volumes in [0.0, 1.0], indexed by bus_index(); the constructor fills 1.0. */
    std::array<float, k_audio_bus_count> bus_volumes_{};

    std::string sounds_dir_;
  };

} // namespace corundum::audio
