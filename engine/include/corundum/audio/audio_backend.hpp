// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::audio {

  /** @brief Opaque handle to a sound loaded by load_sound(). A default-constructed handle is
   *  invalid (@c value 0). */
  struct SoundHandle {
    std::uint32_t value{};

    [[nodiscard]] bool is_valid() const noexcept { return value != 0; }

    bool operator==(const SoundHandle &) const = default;
  };

  /** @brief A mixer bus: every voice is summed into one bus, and every bus into Master. */
  enum class AudioBus : std::uint8_t {
    Master,   ///< The engine output bus; other buses sum into it. A voice routed here takes only master volume.
    Sound,    ///< One-shot and looping sound effects.
    Music,    ///< Streamed music.
    Ambience, ///< Looping background beds.
    Voice,    ///< Dialogue and narration.
    Count,    ///< Not a bus: marks the end of the table, so k_audio_bus_count stays in step.
  };

  /** @brief Number of buses; sizes the per-bus volume tables. */
  constexpr std::size_t k_audio_bus_count = static_cast<std::size_t>(AudioBus::Count);

  /** @brief Table index of @p bus. */
  [[nodiscard]] constexpr std::size_t bus_index(AudioBus bus) noexcept {
    return static_cast<std::size_t>(bus);
  }

  /** @brief Identifies one playing voice.
   *
   *  @c generation changes each time the underlying slot is reused, so an id that outlives its
   *  voice never resolves to a later sound in the same slot. A default-constructed id (index 0)
   *  is never valid.
   */
  struct SoundInstanceId {
    std::uint32_t index{};

    std::uint32_t generation{};

    [[nodiscard]] bool is_valid() const noexcept { return index != 0; }

    bool operator==(const SoundInstanceId &) const = default;
  };

  /** @brief How AudioBackend::play_music() plays a streamed track. */
  struct MusicPlayback {
    /** @brief Seconds to fade in from silence; a track this one replaces fades out over the same
     *  time. 0 starts at full volume and cuts the old track. */
    float fade_in_seconds{};

    /** @brief Seconds of silence between the end of the track and its restart. */
    float loop_delay_seconds{};
  };

  /** @brief Platform-independent audio backend interface.
   *
   * Concrete implementations are created via factory functions
   * (e.g. make_miniaudio_backend) and owned through a
   * std::unique_ptr<AudioBackend>.
   *
   * @note Not thread-safe for concurrent calls from multiple threads — call only
   *       from the game thread. The internal implementation may use a separate
   *       audio thread for mixing, but the API itself is single-threaded.
   */
  class AudioBackend {
  public:
    virtual ~AudioBackend() = default;

    /** @brief Load a sound file and return an opaque handle.
     *  @param path  File path to the sound asset (e.g. "data/sounds/coin.ogg").
     *  @return A non-zero SoundHandle on success, or an error message.
     *  @post The sound data is held in the backend until destruction.
     */
    [[nodiscard]] virtual std::expected<SoundHandle, std::string> load_sound(std::string_view path) = 0;

    /** @brief Start playback of a previously loaded sound on @p bus.
     *  @param handle  SoundHandle returned by load_sound().
     *  @param volume  Playback volume in [0.0, 1.0].
     *  @param loop    If true the sound loops indefinitely.
     *  @param bus     Mixer bus the voice is routed to.
     *  @return A valid SoundInstanceId, or an invalid one when the voice could not start.
     *  @pre @p handle must be a valid non-zero handle from load_sound().
     */
    [[nodiscard]] virtual SoundInstanceId play(SoundHandle handle, float volume = 1.0f, bool loop = false,
                                               AudioBus bus = AudioBus::Sound) = 0;

    /** @brief Stop voice @p id over @p fade_out_seconds (0 stops at once). No-op for a stale id. */
    virtual void stop(SoundInstanceId id, float fade_out_seconds = 0.f) = 0;

    /** @brief Set voice @p id's volume in [0.0, 1.0]. No-op for a stale id. */
    virtual void set_volume(SoundInstanceId id, float volume) = 0;

    /** @brief Set voice @p id's pitch multiplier (1.0 leaves it unchanged). No-op for a stale id.
     *  @note A non-positive @p pitch is ignored. */
    virtual void set_pitch(SoundInstanceId id, float pitch) = 0;

    /** @brief Set @p bus's volume in [0.0, 1.0]; Master is the engine output. */
    virtual void set_bus_volume(AudioBus bus, float volume) = 0;

    /** @brief Pause or resume output without discarding playing sounds.
     *
     *  While paused the backend emits silence and holds each voice at its current
     *  position, so resuming continues where playback stopped rather than jumping
     *  ahead by the paused interval.
     *
     *  @param paused  True to pause output, false to resume.
     */
    virtual void set_paused(bool paused) = 0;

    /** @brief Stream @p path as the music track, fading out any current track.
     *
     *  The track loops forever with MusicPlayback::loop_delay_seconds of silence between plays.
     *  @return ok, or why the file could not be opened (the current track then keeps playing).
     */
    [[nodiscard]] virtual std::expected<void, std::string> play_music(std::string_view path,
                                                                      const MusicPlayback &playback) = 0;

    /** @brief Fade the current music track out over @p fade_out_seconds (0 stops at once).
     *  No-op with no track. */
    virtual void stop_music(float fade_out_seconds) = 0;

    /** @brief Per-frame game-thread housekeeping: schedule music loop restarts and reclaim finished
     *  voices. */
    virtual void update() = 0;
  };

} // namespace corundum::audio
