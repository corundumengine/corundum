#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum::audio {

  using SoundHandle = uint32_t;

  /** @brief Platform-independent audio backend interface.
   *
   * Concrete implementations are created via factory functions
   * (e.g. make_sokol_audio_backend) and owned through a
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
  };

} // namespace corundum::audio
