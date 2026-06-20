#pragma once
#include "common/canvas_context.hpp"
#include "common/tool_texture.hpp"
#include "tileset_view.hpp"
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <vector>

namespace tools {
  struct ToolApp;
}

namespace tools::tilemap {

  using CanvasContext = tools::common::CanvasContext;
  using ToolTexture = tools::common::ToolTexture;

  /// Owns a ToolTexture for each tileset in a Tilemap, parallel to Tilemap::tilesets.
  struct TilemapTextureStore {
    std::vector<ToolTexture> textures;
    ToolApp *app{nullptr};

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
  [[nodiscard]] TilemapTextureStore load_tilemap_textures(ToolApp &app,
                                                          const corundum::gameplay::world::tilemap::Tilemap &map);

  /// Rebuild a TilesetView vector from map.tilesets and a parallel texture store.
  [[nodiscard]] std::vector<TilesetView> rebuild_tileset_views(ToolApp &app,
                                                               const corundum::gameplay::world::tilemap::Tilemap &map,
                                                               const TilemapTextureStore &store);

  /// Retrieve the texture and source rectangle for gid.
  /// @pre store.textures must be parallel to map.tilesets (guaranteed by load_tilemap_textures).
  [[nodiscard]] TileTextureView get_tile_texture(ToolApp &app, const TilemapTextureStore &store,
                                                 const corundum::gameplay::world::tilemap::Tilemap &map,
                                                 corundum::gameplay::world::tilemap::TileId gid) noexcept;

  /// Render a single z-index layer of map into the canvas draw list.
  /// @param elapsed_time  Total elapsed seconds, used to drive tile animations.
  void render_tilemap(ToolApp &app, CanvasContext ctx, const corundum::gameplay::world::tilemap::Tilemap &map,
                      const TilemapTextureStore &store, const corundum::gameplay::world::Camera &camera, int z_index,
                      float tile_scale, float elapsed_time);

  /// Return a sorted, deduplicated list of z-indices that are > 0 (above-entity layers).
  [[nodiscard]] std::vector<int> above_z_indices(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept;

} // namespace tools::tilemap
