// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/platform/platform_events.hpp>

#include <utility>

namespace corundum::platform {

  /** @brief Abstract OS window. Concrete implementations live in the platform layer.
   *
   *  @note Corundum is single-window by design: in-game UI (world, panels, maps) is
   *        drawn inside this window, never in additional OS windows.
   *  @note Not thread-safe. Call only from the main thread.
   */
  class Window {
  public:
    virtual ~Window() = default;

    /** @brief @c true while the window is open; the platform also reports it closed once the user
     *  dismisses the window.
     */
    [[nodiscard]] virtual bool is_open() const = 0;

    /** @brief Ask the window to close; is_open() reports it closed from the next query on. */
    virtual void close() = 0;

    /** @brief Poll platform events, translate them to engine input, and report OS lifecycle events.
     *
     *  Presses already latched in @p input are left untouched — clearing them is the simulation
     *  step's job, via input::clear_pressed. Fields set in @p events are edge events observed
     *  since the previous poll; the caller passes a fresh (default-constructed) struct each frame.
     *
     *  @pre This window must be open.
     */
    virtual void poll_game_input(corundum::input::InputState &input, PlatformEvents &events) = 0;

    /** @brief Query the current window dimensions in logical screen coordinates.
     *
     *  @return {width, height}; not framebuffer pixels, so a high-DPI display reports a smaller
     *          pair than the number of pixels actually drawn.
     */
    [[nodiscard]] virtual std::pair<int, int> size() const = 0;

    /** @brief Enable or disable vertical synchronisation. */
    virtual void set_vsync(bool enabled) = 0;

    /** @brief Opaque handle to the window in the API of the backend that created it.
     *
     *  Its meaning is defined by the linked backend, so only that backend's code may interpret
     *  it. May be @c nullptr when the backend holds no live window object.
     */
    [[nodiscard]] virtual void *native_handle() const = 0;
  };

} // namespace corundum::platform
