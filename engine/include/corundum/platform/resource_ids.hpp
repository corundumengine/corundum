// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>

namespace corundum::platform {

/** Renderer texture id. Invalid (0) is never issued, and ids are never reused. */
enum class TextureId : std::uint32_t { Invalid = 0 };

/** Renderer font id. Invalid (0) is never issued; the first id a renderer hands out (1) is reserved. */
enum class FontId : std::uint32_t { Invalid = 0 };

} // namespace corundum::platform
