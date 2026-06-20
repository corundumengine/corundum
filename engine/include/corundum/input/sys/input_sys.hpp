#pragma once
#include <corundum/input/actions.hpp>

namespace corundum::platform {
  class Window;
}

namespace corundum::input::sys {

  /** @brief Read pending input from @p window and update @p state.
   *
   * Clears pressed flags from the previous frame before polling so that
   * pressed is only true on the frame the key went down.
   *
   *  @param[in,out] state   Snapshot updated with the current frame's events.
   *  @param[in]     window  Source window; must be open.
   *  @pre Must be called once per frame before any system reads @p state.
   *  @post state.pressed is cleared then repopulated for this frame.
   *  @thread_safety Must be called from the main thread only.
   */
  void poll(corundum::input::InputState &state, corundum::platform::Window &window) noexcept;

} // namespace corundum::input::sys
