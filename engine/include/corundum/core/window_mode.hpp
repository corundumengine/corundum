// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

namespace corundum::core {

/** How the game window occupies the display.
 *
 *  Fullscreen is borderless at the monitor's desktop resolution: the display mode never changes,
 *  so switching and alt-tab are instant.
 */
enum class WindowMode : std::uint8_t {
  Windowed,
  Fullscreen,
};

/** Stable JSON spelling: "windowed" or "fullscreen". */
[[nodiscard]] constexpr std::string_view window_mode_name(WindowMode mode) noexcept {
  return mode == WindowMode::Fullscreen ? "fullscreen" : "windowed";
}

/** Inverse of window_mode_name(); nullopt for any other string. */
[[nodiscard]] constexpr std::optional<WindowMode> parse_window_mode(std::string_view name) noexcept {
  if (name == "windowed")
    return WindowMode::Windowed;
  if (name == "fullscreen")
    return WindowMode::Fullscreen;
  return std::nullopt;
}

} // namespace corundum::core
