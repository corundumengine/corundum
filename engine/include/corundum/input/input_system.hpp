// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/platform/platform_events.hpp>

namespace corundum::platform {
  class Window;
}

namespace corundum::input {

  /** @brief Read pending input and OS lifecycle events from @p window.
   *
   * Forwards to @c Window::poll_game_input, which translates this frame's input events and
   * reports the OS events it observed since the previous poll. Held flags are overwritten with
   * the window's current key state; pressed flags, the mouse click, and the scroll delta
   * accumulate, so a press survives every render frame that runs no fixed step.
   *
   *  @param[in,out] state   Snapshot accumulated into; this call does not clear it.
   *  @param[in]     window  Source window; must be open.
   *  @param[out]    events  Freshly-observed OS lifecycle events; caller supplies a fresh struct.
   *  @pre Must be called once per frame before any system reads @p state.
   *  @pre @p state must be cleared with @c clear_pressed each simulation step, or a
   *       latched press is re-observed on every subsequent read.
   *  @thread_safety Must be called from the main thread only.
   */
  void poll(InputState &state, platform::Window &window, platform::PlatformEvents &events) noexcept;

} // namespace corundum::input
