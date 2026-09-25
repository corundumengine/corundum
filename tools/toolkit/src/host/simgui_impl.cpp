// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// One-time sokol_imgui implementation translation unit.
// SOKOL_METAL / SOKOL_GLCORE come from corundum::platform_config
// (cmake/Platform.cmake); SOKOL_IMGUI_NO_SOKOL_APP is injected here.
// No other translation unit in this link unit must define SOKOL_IMGUI_IMPL.
#include <imgui.h>     // Must precede sokol_imgui.h
#include <sokol_gfx.h> // Must precede sokol_imgui.h
#define SOKOL_IMGUI_IMPL
#include <sokol_imgui.h>
