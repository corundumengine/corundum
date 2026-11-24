// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Spritesmith window and panel layout constants.

namespace tools::spritesmith {

constexpr int WINDOW_W = 1770;
constexpr int WINDOW_H = 1000;

/** Reserved for the File menu bar; ImGui sizes the real bar from the loaded font. */
constexpr int MENU_BAR_H = 24;
constexpr int PANEL_W = 400;
constexpr int STATUS_H = 26;
constexpr int PREVIEW_H = 240;

/** 1370 */
constexpr int CANVAS_W = WINDOW_W - PANEL_W;

/** 974 */
constexpr int CANVAS_H = WINDOW_H - STATUS_H;

/** 734 */
constexpr int PANEL_H = CANVAS_H - PREVIEW_H;

} // namespace tools::spritesmith
