// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// One-time sokol implementation translation unit.
// SOKOL_METAL (Apple) or SOKOL_GLCORE (Windows/Linux) is injected by the
// corundum::platform_config interface target (cmake/Platform.cmake).
// No other translation unit must define SOKOL_IMPL.
#define SOKOL_IMPL
#include <sokol_gfx.h>
