#pragma once
#include "tileset_view.hpp"
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/platform/texture_cache.hpp>
#include <corundum/tool_host/canvas_controller.hpp>
#include <vector>

namespace corundum::tool_host {
  class ToolHost;
}

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

  /// Owns a texture ID for each tileset in a Tilemap, parallel to Tilemap::tilesets.
  struct TilemapTextureStore {
    std::vector<corundum::platform::TextureInfo> textures;
    corundum::tool_host::ToolHost *host{nullptr};

    TilemapTextureStore() = default;
    ~TilemapTextureStore();
    TilemapTextureStore(TilemapTextureStore &&) noexcept;
    TilemapTextureStore &operator=(TilemapTextureStore &&) noexcept;
  };

  /// Resolved texture and source rectangle for a single tile GID.
  /// tex_id is the ImGui-invalid handle when gid is k_empty_tile or out of range.
  struct TileTextureView {
    ImTextureID tex_id{};
    unsigned tex_w{};
    unsigned tex_h{};
    corundum::core::math::IntRect rect{};
  };

  /// Load each tileset PNG referenced by map.tilesets into a TilemapTextureStore.
  /// @throws std::runtime_error if any PNG cannot be opened.
  [[nodiscard]] TilemapTextureStore load_tilemap_textures(corundum::tool_host::ToolHost &host,
                                                          const corundum::gameplay::world::tilemap::Tilemap &map);

  /// Rebuild a TilesetView vector from map.tilesets and a parallel texture store.
  [[nodiscard]] std::vector<TilesetView> rebuild_tileset_views(corundum::tool_host::ToolHost &host,
                                                               const corundum::gameplay::world::tilemap::Tilemap &map,
                                                               const TilemapTextureStore &store);

  /// Retrieve the texture and source rectangle for gid.
  /// @pre store.textures must be parallel to map.tilesets (guaranteed by load_tilemap_textures).
  [[nodiscard]] TileTextureView get_tile_texture(corundum::tool_host::ToolHost &host, const TilemapTextureStore &store,
                                                 const corundum::gameplay::world::tilemap::Tilemap &map,
                                                 corundum::gameplay::world::tilemap::TileId gid) noexcept;

  /// Render a single z-index layer of map into the canvas draw list.
  void render_tilemap(corundum::tool_host::ToolHost &host, CanvasContext ctx,
                      const corundum::gameplay::world::tilemap::Tilemap &map, const TilemapTextureStore &store,
                      float camera_x, float camera_y, int z_index, float tile_scale, float elapsed_time,
                      float elev_step);

  /// Return a sorted, deduplicated list of z-indices that are > 0 (above-entity layers).
  [[nodiscard]] std::vector<int> above_z_indices(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept;

} // namespace tools::tilemap
