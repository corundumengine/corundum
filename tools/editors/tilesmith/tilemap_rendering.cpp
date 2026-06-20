#include "tilemap_rendering.hpp"

#include "layout.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tools::tilemap {
  using namespace tools::common;

  // ── TilemapTextureStore ───────────────────────────────────────────────────────

  TilemapTextureStore::~TilemapTextureStore() {
    for (auto &t : textures)
      destroy_tool_texture(*app, t);
  }

  TilemapTextureStore::TilemapTextureStore(TilemapTextureStore &&) noexcept = default;
  TilemapTextureStore &TilemapTextureStore::operator=(TilemapTextureStore &&) noexcept = default;

  TilemapTextureStore load_tilemap_textures(ToolApp &app, const corundum::gameplay::world::tilemap::Tilemap &map) {
    TilemapTextureStore store;
    store.app = &app;
    store.textures.reserve(map.tilesets.size());
    for (const auto &ts : map.tilesets)
      store.textures.push_back(load_tool_texture(app, ts.info.path));
    return store;
  }

  std::vector<TilesetView> rebuild_tileset_views(ToolApp &app, const corundum::gameplay::world::tilemap::Tilemap &map,
                                                 const TilemapTextureStore &store) {
    std::vector<TilesetView> views;
    views.reserve(map.tilesets.size());
    for (std::size_t i = 0; i < map.tilesets.size(); ++i) {
      const ToolTexture &t = store.textures[i];
      views.push_back({&map.tilesets[i], tex_id(app, t), t.w, t.h});
    }
    return views;
  }

  TileTextureView get_tile_texture(ToolApp &app, const TilemapTextureStore &store,
                                   const corundum::gameplay::world::tilemap::Tilemap &map,
                                   corundum::gameplay::world::tilemap::TileId gid) noexcept {
    const corundum::gameplay::world::tilemap::TilemapTileset *ts =
        corundum::gameplay::world::tilemap::find_tileset(map.tilesets, gid);
    if (!ts)
      return {};

    const std::ptrdiff_t idx = ts - map.tilesets.data();
    const int local_id = static_cast<int>(gid) - static_cast<int>(ts->first_gid);
    const int src_col = local_id % ts->info.columns;
    const int src_row = local_id / ts->info.columns;

    const ToolTexture &tex = store.textures[static_cast<std::size_t>(idx)];
    return {
        tex_id(app, tex),
        tex.w,
        tex.h,
        corundum::core::math::IntRect{
            src_col * ts->info.tile_width,
            src_row * ts->info.tile_height,
            ts->info.tile_width,
            ts->info.tile_height,
        },
    };
  }

  // ── render_tilemap ────────────────────────────────────────────────────────────

  void render_tilemap(ToolApp &app, CanvasContext ctx, const corundum::gameplay::world::tilemap::Tilemap &map,
                      const TilemapTextureStore &store, const corundum::gameplay::world::Camera &camera, int z_index,
                      float tile_scale, float elapsed_time) {
    // Use diamond dimensions (world step) for isometric positioning.
    const int dw = map.diamond_w();
    const float half_tw = static_cast<float>(dw) * tile_scale * 0.5f;
    const float half_th = static_cast<float>(map.diamond_h()) * tile_scale * 0.5f;
    const float x_shift = static_cast<float>(map.height) * half_tw;

    // Diagonal sweep (depth = col + row) for correct isometric draw order.
    const int depth_max = map.width + map.height - 2;

    for (const auto &layer : map.layers) {
      if (!layer.visible || layer.z_index != z_index)
        continue;

      const auto tiles = layer.view(map.width, map.height);

      for (int depth = 0; depth <= depth_max; ++depth) {
        const int col_lo = std::max(0, depth - (map.height - 1));
        const int col_hi = std::min(map.width - 1, depth);

        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const int cell_idx = row * map.width + col;

          corundum::gameplay::world::tilemap::TileId gid;
          if (auto it = layer.animated_cells.find(cell_idx); it != layer.animated_cells.end()) {
            const auto &anim = it->second;
            if (anim.frame_gids.empty()) [[unlikely]]
              continue;
            const int n = static_cast<int>(anim.frame_gids.size());
            const int fidx = static_cast<int>(elapsed_time * anim.fps) % n;
            gid = anim.frame_gids[static_cast<std::size_t>(fidx)];
          } else {
            gid = tiles[row, col];
            if (gid == corundum::gameplay::world::tilemap::k_empty_tile)
              continue;
          }

          const auto [img_id, tex_w, tex_h, src] = get_tile_texture(app, store, map, gid);
          if (!img_id) [[unlikely]]
            continue;

          const corundum::gameplay::world::tilemap::TilemapTileset *ts =
              corundum::gameplay::world::tilemap::find_tileset(map.tilesets, gid);

          // Sprite pixel dimensions — independent from the footprint (diamond step).
          const float scaled_tw = std::round(static_cast<float>(ts->info.tile_width) * tile_scale);
          const float scaled_th = std::round(static_cast<float>(ts->info.tile_height) * tile_scale);

          // adj_iso_x = (col - row + map_h - 1) * half_tw
          const float iso_x = static_cast<float>(col - row) * half_tw + x_shift;
          const float iso_y = static_cast<float>(col + row + 1) * half_th;
          // Pivot offset: shift image so the pivot point aligns with the world anchor.
          const float pivot_x_px = ts ? (ts->info.pivot_x * scaled_tw) : 0.f;
          const float pivot_y_px = ts ? ((1.f - ts->info.pivot_y) * scaled_th) : 0.f;
          const float dst_x = ctx.origin.x + iso_x - pivot_x_px - camera.x;
          const float dst_y = ctx.origin.y + iso_y - pivot_y_px - camera.y;
          const ImVec2 p0 = {dst_x, dst_y};
          const ImVec2 p1 = {dst_x + scaled_tw, dst_y + scaled_th};

          // Cull tiles outside the canvas
          if (p1.x < ctx.origin.x || p0.x > ctx.origin.x + CANVAS_W || p1.y < ctx.origin.y ||
              p0.y > ctx.origin.y + CANVAS_H)
            continue;

          const float tw_f = static_cast<float>(tex_w);
          const float th_f = static_cast<float>(tex_h);
          ImVec2 uv0 = {static_cast<float>(src.x) / tw_f, static_cast<float>(src.y) / th_f};
          ImVec2 uv1 = {static_cast<float>(src.x + src.width) / tw_f, static_cast<float>(src.y + src.height) / th_f};

          if (auto fit = layer.flip_flags.find(cell_idx); fit != layer.flip_flags.end()) {
            const uint8_t f = fit->second;
            if (f & corundum::gameplay::world::tilemap::k_flip_h)
              std::swap(uv0.x, uv1.x);
            if (f & corundum::gameplay::world::tilemap::k_flip_v)
              std::swap(uv0.y, uv1.y);
          }

          ctx.dl->AddImage(img_id, p0, p1, uv0, uv1);
        }
      }
    }
  }

  // ── above_z_indices ───────────────────────────────────────────────────────────

  std::vector<int> above_z_indices(const corundum::gameplay::world::tilemap::Tilemap &map) noexcept {
    std::vector<int> result;
    for (const auto &layer : map.layers)
      if (layer.z_index > 0)
        result.push_back(layer.z_index);
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
  }

} // namespace tools::tilemap
