// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "tilemap_rendering.hpp"

#include "layer_visibility.hpp"
#include "layout.hpp"
#include "tileset_view.hpp"
#include <algorithm>
#include <cmath>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/verify.hpp>
#include <corundum/toolkit/host/tool_host.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <cstddef>
#include <imgui.h>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tools::tilesmith {

  TilemapTextureStore::~TilemapTextureStore() {
    clear();
  }

  void TilemapTextureStore::clear() noexcept {
    if (host != nullptr) {
      for (const auto &t : textures)
        host->textures().destroy(t.id);
    }
    textures.clear();
  }

  TilemapTextureStore::TilemapTextureStore(TilemapTextureStore &&) noexcept = default;

  TilemapTextureStore &TilemapTextureStore::operator=(TilemapTextureStore &&other) noexcept {
    // The defaulted move-assignment would drop the destination's old TextureInfos
    // without returning them to the cache, leaking their GPU resources. Release
    // the outgoing set first.
    if (this != &other) {
      clear();
      host = other.host;
      textures = std::move(other.textures);
    }
    return *this;
  }

  TilemapTextureStore load_tilemap_textures(corundum::toolkit::host::ToolHost &host,
                                            const corundum::world::tilemap::Tilemap &map) {
    TilemapTextureStore store;
    store.host = &host;
    store.textures.reserve(map.tilesets.size());
    for (const auto &ts : map.tilesets) {
      auto tex = host.textures().load(ts.info.path);
      if (!tex)
        throw std::runtime_error(tex.error());
      store.textures.push_back(*tex);
    }
    return store;
  }

  std::vector<TilesetView> rebuild_tileset_views(corundum::toolkit::host::ToolHost &host,
                                                 const corundum::world::tilemap::Tilemap &map,
                                                 const TilemapTextureStore &store) {
    std::vector<TilesetView> views;
    views.reserve(map.tilesets.size());
    for (std::size_t i = 0; i < map.tilesets.size(); ++i) {
      const auto &t = store.textures[i];
      views.push_back({
          .tileset = &map.tilesets[i],
          .tex_id = host.imgui_id(t.id),
          .tex_w = t.width,
          .tex_h = t.height,
      });
    }
    return views;
  }

  TileTextureView get_tile_texture(corundum::toolkit::host::ToolHost &host, const TilemapTextureStore &store,
                                   const corundum::world::tilemap::Tilemap &map,
                                   corundum::world::tilemap::TileId gid) noexcept {
    const corundum::world::tilemap::TilemapTileset *ts = corundum::world::tilemap::find_tileset(map.tilesets, gid);
    if (ts == nullptr)
      return {};

    const std::ptrdiff_t idx = ts - map.tilesets.data();
    const auto src = corundum::world::tilemap::tile_source_rect(*ts, gid);

    const auto &tex = store.textures[static_cast<std::size_t>(idx)];
    return {
        .tex_id = host.imgui_id(tex.id),
        .tex_w = tex.width,
        .tex_h = tex.height,
        .rect = src,
    };
  }

  namespace {

    struct PendingTileDraw {
      ImTextureID img_id{};

      ImVec2 p0{};

      ImVec2 p1{};

      ImVec2 uv0{};

      ImVec2 uv1{};

      float depth{};
    };

    /// The invariants one render_tilemap pass holds still for every cell it draws.
    /// @note The pointers are the caller's, live for the whole call and never null.
    struct TilePass {
      corundum::toolkit::host::ToolHost *host{};

      CanvasContext ctx;

      const corundum::world::tilemap::Tilemap *map{};

      const TilemapTextureStore *store{};

      corundum::core::math::IsometricParams iso{};

      float camera_x{};

      float camera_y{};

      float tile_scale{};

      float elapsed_time{};

      int z_index{};
    };

    /// One cell's draw, and whether the ground band has to depth-sort it against entities.
    struct ResolvedCell {
      PendingTileDraw draw{};

      bool depth_sorted{};
    };

    /// The GID to draw for @p cell_index, resolving an animated cell to its current frame.
    /// @return std::nullopt for an empty cell or an animation with no frames.
    [[nodiscard]] std::optional<corundum::world::tilemap::TileId>
    cell_gid(const corundum::world::tilemap::TilemapLayer &layer, int cell_index, float elapsed_time) {
      const auto animated = layer.animated_cells.find(cell_index);
      if (animated == layer.animated_cells.end()) {
        const corundum::world::tilemap::TileId gid = layer.tiles[static_cast<std::size_t>(cell_index)];
        if (gid == corundum::world::tilemap::k_empty_tile)
          return std::nullopt;
        return gid;
      }
      const auto &animation = animated->second;
      if (animation.frame_gids.empty()) [[unlikely]]
        return std::nullopt;
      const int frame_count = static_cast<int>(animation.frame_gids.size());
      const int frame_index = static_cast<int>(elapsed_time * animation.fps) % frame_count;
      return animation.frame_gids[static_cast<std::size_t>(frame_index)];
    }

    /// Screen-space quad, UVs and depth for (@p col, @p row), laid out the way the canvas draws it.
    [[nodiscard]] ResolvedCell resolve_cell(const TilePass &pass, const corundum::world::tilemap::TilemapLayer &layer,
                                            int col, int row, int cell_index, corundum::world::tilemap::TileId gid) {
      const auto [img_id, tex_w, tex_h, src] = get_tile_texture(*pass.host, *pass.store, *pass.map, gid);

      const corundum::world::tilemap::TilemapTileset *tileset =
          corundum::world::tilemap::find_tileset(pass.map->tilesets, gid);

      const auto cell_offset = static_cast<std::size_t>(cell_index);
      const int elevation = cell_offset < layer.elevation.size() ? static_cast<int>(layer.elevation[cell_offset]) : 0;

      const float col_f = static_cast<float>(col);
      const float row_f = static_cast<float>(row);
      const auto world = corundum::core::math::tile_to_world(col_f, row_f, elevation, pass.iso);
      const float iso_x = world.x;
      const float iso_y = world.y + pass.iso.half_th;

      const int local_id = tileset != nullptr ? static_cast<int>(gid) - static_cast<int>(tileset->first_gid) : 0;
      const auto frame = tileset != nullptr ? corundum::world::tilemap::get_tile_frame_offset(tileset->info, local_id)
                                            : corundum::world::tilemap::TileFrameOffset{};
      const auto pivot = tileset != nullptr ? corundum::world::tilemap::get_tile_pivot(tileset->info, local_id)
                                            : corundum::world::tilemap::TilePivot{};
      const float scaled_tw = std::round(static_cast<float>(frame.full_width) * pass.tile_scale);
      const float scaled_th = std::round(static_cast<float>(frame.full_height) * pass.tile_scale);
      const float trim_x_px = std::round(static_cast<float>(frame.trim_x) * pass.tile_scale);
      const float trim_y_px = std::round(static_cast<float>(frame.trim_y) * pass.tile_scale);
      const float drawn_w = std::round(static_cast<float>(src.width) * pass.tile_scale);
      const float drawn_h = std::round(static_cast<float>(src.height) * pass.tile_scale);
      const float pivot_x_px = pivot.x * scaled_tw;
      const float pivot_y_px = corundum::core::math::pivot_top_offset(pivot.y, scaled_th);
      const float dst_x = pass.ctx.origin.x + iso_x - pivot_x_px + trim_x_px - pass.camera_x;
      const float dst_y = pass.ctx.origin.y + iso_y - pivot_y_px + trim_y_px - pass.camera_y;

      const float tex_w_f = static_cast<float>(tex_w);
      const float tex_h_f = static_cast<float>(tex_h);
      PendingTileDraw draw{
          .img_id = img_id,
          .p0 = {dst_x, dst_y},
          .p1 = {dst_x + drawn_w, dst_y + drawn_h},
          .uv0 = {static_cast<float>(src.x) / tex_w_f, static_cast<float>(src.y) / tex_h_f},
          .uv1 = {static_cast<float>(src.x + src.width) / tex_w_f, static_cast<float>(src.y + src.height) / tex_h_f},
          .depth = 0.f,
      };

      if (const auto flip = layer.flip_flags.find(cell_index); flip != layer.flip_flags.end()) {
        if ((flip->second & corundum::world::tilemap::k_flip_h) != 0)
          std::swap(draw.uv0.x, draw.uv1.x);
        if ((flip->second & corundum::world::tilemap::k_flip_v) != 0)
          std::swap(draw.uv0.y, draw.uv1.y);
      }

      const bool depth_sorted = pass.z_index == 0 && elevation > 0;
      if (depth_sorted)
        draw.depth = corundum::core::math::iso_depth_key(col_f, row_f, static_cast<float>(elevation), pass.iso.half_th,
                                                         pass.iso.elev_step);
      return {.draw = draw, .depth_sorted = depth_sorted};
    }

    /// Draw @p draw immediately to the canvas.
    void draw_now(CanvasContext ctx, const PendingTileDraw &draw) {
      ctx.dl->AddImage(draw.img_id, draw.p0, draw.p1, draw.uv0, draw.uv1);
    }

    /// Whether @p draw lands outside the canvas viewport.
    [[nodiscard]] bool off_canvas(const CanvasContext &ctx, const PendingTileDraw &draw) noexcept {
      return draw.p1.x < ctx.origin.x || draw.p0.x > ctx.origin.x + CANVAS_W || draw.p1.y < ctx.origin.y ||
             draw.p0.y > ctx.origin.y + CANVAS_H;
    }

    /// The cell's resolved draw, or std::nullopt when it is empty, has no texture, or lands off-canvas.
    [[nodiscard]] std::optional<ResolvedCell>
    visible_cell(const TilePass &pass, const corundum::world::tilemap::TilemapLayer &layer, int col, int row) {
      const int cell_index = (row * pass.map->width) + col;
      const std::optional<corundum::world::tilemap::TileId> gid = cell_gid(layer, cell_index, pass.elapsed_time);
      if (!gid)
        return std::nullopt;
      const ResolvedCell cell = resolve_cell(pass, layer, col, row, cell_index, *gid);
      if (cell.draw.img_id == 0 || off_canvas(pass.ctx, cell.draw))
        return std::nullopt;
      return cell;
    }

    /// Queue every tile of @p layer into @p pending, in depth order, or draw it straight to the canvas.
    void draw_layer(const TilePass &pass, const corundum::world::tilemap::TilemapLayer &layer,
                    std::vector<PendingTileDraw> &pending) {
      const int depth_max = pass.map->width + pass.map->height - 2;
      for (int depth = 0; depth <= depth_max; ++depth) {
        const int col_lo = std::max(0, depth - (pass.map->height - 1));
        const int col_hi = std::min(pass.map->width - 1, depth);
        for (int col = col_lo; col <= col_hi; ++col) {
          const int row = depth - col;
          const std::optional<ResolvedCell> cell = visible_cell(pass, layer, col, row);
          if (!cell)
            continue;
          if (cell->depth_sorted)
            pending.push_back(cell->draw);
          else
            draw_now(pass.ctx, cell->draw);
        }
      }
    }

  } // namespace

  void render_tilemap(corundum::toolkit::host::ToolHost &host, CanvasContext ctx,
                      const corundum::world::tilemap::Tilemap &map, const LayerVisibility &layer_visible,
                      const TilemapTextureStore &store, float camera_x, float camera_y, int z_index, float tile_scale,
                      float elapsed_time, float elev_step) {
    corundum::core::verify(layer_visible.size() == map.layers.size(),
                           "render_tilemap: layer visibility is out of step with the map");
    const TilePass pass{
        .host = &host,
        .ctx = ctx,
        .map = &map,
        .store = &store,
        .iso = corundum::core::math::compute_isometric_params(map.diamond_w(), map.diamond_h(), map.height, tile_scale,
                                                              elev_step),
        .camera_x = camera_x,
        .camera_y = camera_y,
        .tile_scale = tile_scale,
        .elapsed_time = elapsed_time,
        .z_index = z_index,
    };

    std::vector<PendingTileDraw> pending;
    for (std::size_t layer_index = 0; layer_index < map.layers.size(); ++layer_index) {
      if (!layer_visible[layer_index] || map.layers[layer_index].z_index != z_index)
        continue;
      draw_layer(pass, map.layers[layer_index], pending);
    }

    std::ranges::stable_sort(
        pending, [](const PendingTileDraw &a, const PendingTileDraw &b) noexcept { return a.depth < b.depth; });
    for (const auto &draw : pending)
      draw_now(ctx, draw);
  }

  std::vector<int> above_z_indices(const corundum::world::tilemap::Tilemap &map) {
    std::vector<int> result;
    for (const auto &layer : map.layers)
      if (layer.z_index > 0)
        result.push_back(layer.z_index);
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
  }

} // namespace tools::tilesmith
