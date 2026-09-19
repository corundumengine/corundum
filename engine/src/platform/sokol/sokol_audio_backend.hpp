// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/audio/audio_backend.hpp>

#include <memory>

namespace corundum::platform::sokol {

  /** @brief Create a sokol_audio-backed AudioBackend.
   *
   * Initialises sokol_audio with a 44100 Hz stereo stream callback. The returned
   * backend is always usable: if saudio_setup() fails, load_sound() reports an
   * error, play() and set_master_volume() become no-ops, and destruction is safe.
   *
   * @note sokol_audio is process-global. Only one backend may own it at a time —
   *       a second instance's setup fails, and destroying one disables audio for
   *       the whole process.
   * @note Must be called from the main thread.
   *
   * @return Owning pointer to the initialised AudioBackend.
   */
  [[nodiscard]] std::unique_ptr<corundum::audio::AudioBackend> make_sokol_audio_backend();

} // namespace corundum::platform::sokol
