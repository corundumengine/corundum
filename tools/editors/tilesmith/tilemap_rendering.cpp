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
    const auto src = corundum::gameplay::world::tilemap::tile_source_rect(*ts, gid);

    const ToolTexture &tex = store.textures[static_cast<std::size_t>(idx)];
    return {
        tex_id(app, tex),
        tex.w,
        tex.h,
        src,
    };
  }

  // ── render_tilemap ────────────────────────────────────────────────────────────

  namespace {

    // Deferred draw for an elevated ground tile — collected so it can be depth-sorted
    // against other elevated tiles before drawing, since a raised tile can visually
    // overlap tiles from a different diagonal that would otherwise draw over it.
    struct PendingTileDraw {
      ImTextureID img_id;
      ImVec2 p0;
      ImVec2 p1;
      ImVec2 uv0;
      ImVec2 uv1;
      float depth;
    };

  } // namespace

  void render_tilemap(ToolApp &app, CanvasContext ctx, const corundum::gameplay::world::tilemap::Tilemap &map,
                      const TilemapTextureStore &store, const corundum::gameplay::world::Camera &camera, int z_index,
                      float tile_scale, float elapsed_time, float elev_step) {
    // Use diamond dimensions (world step) for isometric positioning.
    const auto iso = corundum::core::math::compute_iso_params(map.diamond_w(), map.diamond_h(), map.height, tile_scale);

    // Diagonal sweep (depth = col + row) for correct isometric draw order.
    const int depth_max = map.width + map.height - 2;

    // Elevated ground tiles (z_index == 0) are deferred here and depth-sorted below,
    // mirroring the engine's collect_tile_layer split — flat tiles stay on the fast
    // immediate-draw path since their screen position already matches draw order.
    std::vector<PendingTileDraw> pending;

    for (const auto &layer : map.layers) {
      if (!layer.visible || layer.z_index != z_index)
        continue;

      const auto tiles = map.layer_view(layer);

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

          const int elev = (!layer.elevation.empty() && static_cast<std::size_t>(cell_idx) < layer.elevation.size())
                               ? static_cast<int>(layer.elevation[static_cast<std::size_t>(cell_idx)])
                               : 0;

          const auto world =
              corundum::core::math::tile_to_world(col, row, elev, iso.half_tw, iso.half_th, elev_step, iso.x_origin);
          const float iso_x = world.x;
          // Anchor at the southern (bottom) vertex so the tile image fills the diamond cell.
          const float iso_y = world.y + corundum::core::math::diamond_cell_height(iso.half_th);
          // Pivot and frame size are per-tile (TilesetInfo::tile_pivot_*/tile_full_*), since
          // spritepacker computes them per sprite — there's no tileset-wide default to fall back to.
          // Pivot is always measured against the full (untrimmed) frame, not the trimmed size, so
          // sprites sharing one canvas convention (e.g. a wall body and its separately-authored
          // topper) stay aligned to each other regardless of how much padding either one had
          // trimmed away — then shift by the trim offset within that frame; only the trimmed
          // region (src, already the packed atlas rect) is actually drawn.
          const int local_id = ts ? static_cast<int>(gid) - static_cast<int>(ts->first_gid) : 0;
          const auto frame = ts ? corundum::gameplay::world::tilemap::get_tile_frame_offset(ts->info, local_id)
                                : corundum::gameplay::world::tilemap::TileFrameOffset{};
          const auto pivot = ts ? corundum::gameplay::world::tilemap::get_tile_pivot(ts->info, local_id)
                                : corundum::gameplay::world::tilemap::TilePivot{};
          const float scaled_tw = std::round(static_cast<float>(frame.full_width) * tile_scale);
          const float scaled_th = std::round(static_cast<float>(frame.full_height) * tile_scale);
          const float trim_x_px = std::round(static_cast<float>(frame.trim_x) * tile_scale);
          const float trim_y_px = std::round(static_cast<float>(frame.trim_y) * tile_scale);
          const float drawn_w = std::round(static_cast<float>(src.width) * tile_scale);
          const float drawn_h = std::round(static_cast<float>(src.height) * tile_scale);
          const float pivot_x_px = pivot.x * scaled_tw;
          const float pivot_y_px = (1.f - pivot.y) * scaled_th;
          const float dst_x = ctx.origin.x + iso_x - pivot_x_px + trim_x_px - camera.x;
          const float dst_y = ctx.origin.y + iso_y - pivot_y_px + trim_y_px - camera.y;
          const ImVec2 p0 = {dst_x, dst_y};
          const ImVec2 p1 = {dst_x + drawn_w, dst_y + drawn_h};

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

          if (z_index == 0 && elev > 0) {
            const float depth_key = corundum::core::math::iso_depth_key(
                static_cast<float>(col), static_cast<float>(row), static_cast<float>(elev), iso.half_th, elev_step);
            pending.push_back({img_id, p0, p1, uv0, uv1, depth_key});
          } else {
            ctx.dl->AddImage(img_id, p0, p1, uv0, uv1);
          }
        }
      }
    }

    if (!pending.empty()) {
      std::ranges::stable_sort(
          pending, [](const PendingTileDraw &a, const PendingTileDraw &b) noexcept { return a.depth < b.depth; });
      for (const auto &p : pending)
        ctx.dl->AddImage(p.img_id, p.p0, p.p1, p.uv0, p.uv1);
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
