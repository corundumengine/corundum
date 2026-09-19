// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// One-time sokol implementation translation unit.
// SOKOL_METAL (Apple) or SOKOL_GLCORE (other platforms) is injected by CMake
// compile definitions; the D3D11 backend is not wired up yet.
// No other translation unit must define SOKOL_IMPL.
#define SOKOL_IMPL
#include <sokol_gfx.h>
