#pragma once

namespace corundum::debug {

  /** @brief Mutable debug-HUD scratch stored on Engine.
   *
   *  Owns the on/off flag and the EMA-smoothed FPS used by the overlay.
   *  Plain aggregate matching the sibling-substruct pattern (audio,
   *  input_state, render); no encapsulation added.
   */
  struct HudState {
    /** @brief When true, render_frame() draws the debug overlay. */
    bool enabled = false;
    /** @brief EMA-smoothed render FPS, updated each frame the overlay runs. */
    float smoothed_fps = 0.f;
  };

} // namespace corundum::debug
