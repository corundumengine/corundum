#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <memory>

namespace corundum::platform::glfw {

  /** @brief Create a sokol_audio-backed AudioBackend.
   *
   * Initialises sokol_audio with a 44100 Hz stereo stream callback.
   * The returned backend is valid even if saudio_setup() fails — all
   * operations become no-ops and destruction is safe.
   *
   * Must be called from the main thread.
   *
   * @return Owning pointer to the initialised AudioBackend.
   */
  [[nodiscard]] std::unique_ptr<corundum::audio::AudioBackend> make_sokol_audio_backend();

} // namespace corundum::platform::glfw
