// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>
#include <imgui.h>

namespace tools::tilesmith {

/** Tool-local view of a single tileset, with the GLFW texture pre-resolved.
 *
 *  Constructed once in main.cpp (which owns TilemapTextureStore) and passed to
 *  render functions so they do not need to include engine/platform/sfml/ headers.
 *
 *  Lifetime: all pointers must remain valid for the duration of the frame in
 *  which the view is used.
 */
struct TilesetView {
  /** Pointer into map.tilesets. */
  const corundum::world::tilemap::TilemapTileset *tileset;

  /** Pre-resolved from sf::Texture::getNativeHandle() by main.cpp. */
  ImTextureID tex_id;

  /** Texture width in pixels. */
  unsigned tex_w;

  /** Texture height in pixels. */
  unsigned tex_h;
};

} // namespace tools::tilesmith
