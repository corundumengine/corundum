#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <imgui.h>

namespace tools::tilemap {

  /**
   * @brief Tool-local view of a single tileset, with the GLFW texture pre-resolved.
   *
   * Constructed once in main.cpp (which owns TilemapTextureStore) and passed to
   * render functions so they do not need to include engine/platform/sfml/ headers.
   *
   * Lifetime: all pointers must remain valid for the duration of the frame in
   * which the view is used.
   */
  struct TilesetView {
    const corundum::gameplay::world::tilemap::TilemapTileset *tileset; ///< Pointer into map.tilesets.
    ImTextureID tex_id; ///< Pre-resolved from sf::Texture::getNativeHandle() by main.cpp.
    unsigned tex_w;     ///< Texture width in pixels.
    unsigned tex_h;     ///< Texture height in pixels.
  };

} // namespace tools::tilemap
