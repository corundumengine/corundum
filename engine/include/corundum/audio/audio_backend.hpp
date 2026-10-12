// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::audio {

  using SoundHandle = uint32_t;

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
   *       audio thread for mixing (behind a mutex), but the API itself is
   *       single-threaded.
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

    /** @brief Start playback of a previously loaded sound.
     *  @param handle  SoundHandle returned by load_sound().
     *  @param volume  Playback volume in [0.0, 1.0].
     *  @param loop    If true the sound loops indefinitely.
     *  @pre @p handle must be a valid non-zero handle from load_sound().
     */
    virtual void play(SoundHandle handle, float volume = 1.0f, bool loop = false) = 0;

    /** @brief Set the global master volume.
     *  @param volume  Volume level in [0.0, 1.0].
     */
    virtual void set_master_volume(float volume) = 0;

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
     *  Backends without music keep this default, which reports it unsupported.
     *  @return ok, or why the file could not be opened (the current track then keeps playing).
     */
    [[nodiscard]] virtual std::expected<void, std::string> play_music(std::string_view /*path*/,
                                                                      const MusicPlayback & /*playback*/) {
      return std::unexpected(std::string{"[audio] music is not supported by this backend"});
    }

    /** @brief Fade the current music track out over @p fade_out_seconds (0 stops at once).
     *  No-op with no track. */
    virtual void stop_music(float /*fade_out_seconds*/) {}

    /** @brief Set the music group's volume in [0.0, 1.0]; the master volume applies on top. */
    virtual void set_music_volume(float /*volume*/) {}

    /** @brief Per-frame game-thread housekeeping: schedule music loop restarts and reclaim finished
     *  voices. */
    virtual void update() {}
  };

} // namespace corundum::audio
