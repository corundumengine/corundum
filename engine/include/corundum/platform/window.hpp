// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/window_mode.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/platform_events.hpp>

#include <string>
#include <utility>

namespace corundum::input {
  class InputMapper;
} // namespace corundum::input

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

    /** @brief Present the window on screen.
     *
     *  Backends create windows hidden so the OS never composites an unpainted surface while the
     *  game loads its assets: the owner calls show() once the first frame has been presented, so
     *  the first thing the player sees is the game, not a blank window. Idempotent.
     */
    virtual void show() = 0;

    /** @brief Pump platform events, feed this poll's physical input to @p mapper, and report OS lifecycle events.
     *
     *  The backend reports physical input only (keys, mouse buttons, cursor, scroll, the active
     *  gamepad); @p mapper owns the bindings that turn it into Actions. Fields set in @p events are
     *  edge events observed since the previous poll; the caller passes a fresh struct each frame.
     *
     *  @pre This window must be open, and the caller has called mapper.begin_poll().
     */
    virtual void poll_game_input(corundum::input::InputMapper &mapper, PlatformEvents &events) = 0;

    /** @brief Query the current window dimensions in logical screen coordinates.
     *
     *  @return {width, height}; not framebuffer pixels, so a high-DPI display reports a smaller
     *          pair than the number of pixels actually drawn.
     */
    [[nodiscard]] virtual std::pair<int, int> size() const = 0;

    /** @brief Enable or disable vertical synchronisation. */
    virtual void set_vsync(bool enabled) = 0;

    /** @brief Switch between windowed and borderless fullscreen on the monitor the window mostly covers.
     *
     *  Returning to Windowed restores the position and size the window had before going fullscreen.
     *  A no-op when @p mode is already current.
     */
    virtual void set_window_mode(core::WindowMode mode) = 0;

    /** @brief The current window mode. */
    [[nodiscard]] virtual core::WindowMode window_mode() const = 0;

    /** @brief Label for @p input as printed on the player's hardware, for prompts and a controls menu.
     *
     *  Printable keys follow the active keyboard layout (the key at US "W" reads "Z" on AZERTY);
     *  everything else falls back to input::name_of().
     */
    [[nodiscard]] virtual std::string input_label(corundum::input::PhysicalInput input) const = 0;

    /** @brief Opaque handle to the window in the API of the backend that created it.
     *
     *  Its meaning is defined by the linked backend, so only that backend's code may interpret
     *  it. May be @c nullptr when the backend holds no live window object.
     */
    [[nodiscard]] virtual void *native_handle() const = 0;
  };

} // namespace corundum::platform
