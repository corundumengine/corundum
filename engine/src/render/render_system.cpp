// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/render_system.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/world_aggregates.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

using corundum::core::math::IntRect;
using corundum::sprites::AnimId;
using corundum::sprites::k_null_sprite_id;
using corundum::sprites::k_num_anim_ids;
using corundum::sprites::SpriteId;

namespace corundum::render {

  std::optional<SpriteFrameIndex::Entry> SpriteFrameIndex::get(SpriteId sprite_id, AnimId anim_id,
                                                               uint8_t frame_index) const noexcept {
    if (sprite_id == k_null_sprite_id)
      return std::nullopt;

    const auto aid = static_cast<uint8_t>(anim_id);
    if (aid >= k_num_anim_ids)
      return std::nullopt;

    const auto sid = static_cast<std::size_t>(sprite_id);
    if (sid >= tex_by_sprite_id.size())
      return std::nullopt;
    const std::optional<uint32_t> &tex_id = tex_by_sprite_id[sid];
    if (!tex_id.has_value())
      return std::nullopt;

    const std::size_t slot = (sid * k_num_anim_ids) + aid;
    if (slot >= anim_frame_counts.size() || slot >= anim_offsets.size() || frame_index >= anim_frame_counts[slot])
      return std::nullopt;

    const std::size_t rect_index = anim_offsets[slot] + frame_index;
    if (rect_index >= frame_rects.size())
      return std::nullopt;

    const float woff = sid < walk_offsets.size() ? walk_offsets[sid] : 0.f;
    return Entry{.tex_id = *tex_id, .src = frame_rects[rect_index], .walk_offset = woff};
  }

  void snapshot_previous_step(render::RenderState &state, const corundum::world::Scene &scene) noexcept {
    const corundum::entities::TransformTable &transforms = scene.world.transforms;
    const auto entities = transforms.active_entities();
    for (std::uint32_t slot = 0; slot < transforms.count; ++slot) {
      const corundum::entities::EntityId e = entities[slot];
      state.prev_col[e.index] = transforms.col[slot];
      state.prev_row[e.index] = transforms.row[slot];
      state.prev_generation[e.index] = e.generation;
    }
    state.prev_cam_x = scene.camera.x;
    state.prev_cam_y = scene.camera.y;
    state.prev_zoom = scene.camera.zoom;
  }

  core::math::Vec2 interpolated_tile_position(const render::RenderState &state, corundum::entities::EntityId e,
                                              float col, float row, float alpha) noexcept {
    if (e.index >= corundum::entities::k_max_entities || state.prev_generation[e.index] != e.generation)
      return {.x = col, .y = row};
    return {
        .x = std::lerp(state.prev_col[e.index], col, alpha),
        .y = std::lerp(state.prev_row[e.index], row, alpha),
    };
  }

  namespace {

    void render_tilemap(platform::Renderer &r, const RenderState &state, int z_index, const core::GameConfig &cfg,
                        const world::Scene &scene, float cam_x, float cam_y, float zoom, int window_w, int window_h);

    void render_chunk(platform::Renderer &r, const RenderState &state, const ChunkEntry &chunk, int z_index,
                      const core::GameConfig &cfg, const world::Scene &scene, float cam_x, float cam_y, float zoom,
                      int window_w, int window_h);

    void render_ground_layer(platform::Renderer &r, RenderState &state, const core::GameConfig &cfg,
                             const world::Scene &scene, float alpha, float cam_x, float cam_y, float zoom, int win_w,
                             int win_h);

  } // namespace

  namespace {

    /** @brief Camera position and zoom computed for one frame's render. */
    struct CameraBlend {
      float x{};

      float y{};

      float zoom{1.f};
    };

    /** @brief Blend the camera from its start-of-step snapshot to the current scene camera by @p alpha. */
    CameraBlend blend_camera(const render::RenderState &state, const corundum::world::Scene &scene,
                             float alpha) noexcept {
      return {
          .x = std::lerp(state.prev_cam_x, scene.camera.x, alpha),
          .y = std::lerp(state.prev_cam_y, scene.camera.y, alpha),
          .zoom = std::lerp(state.prev_zoom, scene.camera.zoom, alpha),
      };
    }

    /// Rebuild the cached union of the active chunks' above-ground bands when the set changed.
    void refresh_above_ground_bands(render::RenderState &state) {
      if (!state.chunks.consume_dirty())
        return;
      state.above_z_cache.clear();
      for (const auto &chunk : state.chunks.active())
        state.above_z_cache.insert(state.above_z_cache.end(), chunk.above_z.begin(), chunk.above_z.end());
      std::ranges::sort(state.above_z_cache);
      state.above_z_cache.erase(std::ranges::unique(state.above_z_cache).begin(), state.above_z_cache.end());
    }

    void draw_world_frame(corundum::platform::Renderer &r, render::RenderState &state,
                          const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene, float alpha,
                          const CameraBlend &camera, core::math::Vec2 viewport, int win_w, int win_h) {
      const core::math::Vec2 top_left{.x = camera.x, .y = camera.y};
      r.set_world_view(top_left, viewport, camera.zoom);
      render_ground_layer(r, state, cfg, scene, alpha, camera.x, camera.y, camera.zoom, win_w, win_h);
      refresh_above_ground_bands(state);

      for (const int z : state.above_z_cache) {
        r.set_world_view(top_left, viewport, camera.zoom);
        for (const auto &chunk : state.chunks.active())
          render_chunk(r, state, chunk, z, cfg, scene, camera.x, camera.y, camera.zoom, win_w, win_h);
      }
    }

    void draw_map_frame(corundum::platform::Renderer &r, render::RenderState &state, const core::GameConfig &cfg,
                        const corundum::world::Scene &scene, float alpha, const CameraBlend &camera,
                        core::math::Vec2 viewport, int win_w, int win_h) {
      const core::math::Vec2 top_left{.x = camera.x, .y = camera.y};
      r.set_world_view(top_left, viewport, camera.zoom);
      render_ground_layer(r, state, cfg, scene, alpha, camera.x, camera.y, camera.zoom, win_w, win_h);

      for (const int z : state.map_data.above_z) {
        r.set_world_view(top_left, viewport, camera.zoom);
        render_tilemap(r, state, z, cfg, scene, camera.x, camera.y, camera.zoom, win_w, win_h);
      }
    }

  } // namespace

  void render(corundum::platform::Renderer &r, render::RenderState &state, const corundum::core::GameConfig &cfg,
              const corundum::world::Scene &scene, float alpha, int win_w, int win_h) {
    const core::math::Vec2 viewport{.x = static_cast<float>(win_w), .y = static_cast<float>(win_h)};
    const CameraBlend camera = blend_camera(state, scene, alpha);
    if (state.mode == render::RenderMode::World)
      draw_world_frame(r, state, cfg, scene, alpha, camera, viewport, win_w, win_h);
    else
      draw_map_frame(r, state, cfg, scene, alpha, camera, viewport, win_w, win_h);
    r.reset_screen_view();
  }

  namespace {

    /// Parameters for rendering one layer of a tilemap (single-map or world-chunk).
    struct TileRenderParams {
      const corundum::world::tilemap::Tilemap *tilemap = nullptr;
      const std::vector<uint32_t> *tex_ids = nullptr;
      core::math::IsometricParams iso{};
      float camera_x = 0.f;   ///< Camera top-left x in world pixels (interpolated).
      float camera_y = 0.f;   ///< Camera top-left y in world pixels (interpolated).
      float viewport_w = 0.f; ///< Viewport width in world pixels.
      float viewport_h = 0.f; ///< Viewport height in world pixels.
      int chunk_offset_col = 0;
      int chunk_offset_row = 0;
    };

    /// One fully-resolved tile draw, ready to either draw immediately or collect into a depth-sorted list.
    struct ResolvedTile {
      uint32_t tex_id = 0;
      corundum::core::math::IntRect src{};
      corundum::core::math::Vec2 position{};
      float scale = 1.f;
      bool flip_x = false;
      bool flip_y = false;
      float depth = 0.f; ///< iso_depth_key() value, accounting for elevation.
      int elevation = 0; ///< Raw elevation [0-255]; 0 for flat ground.
    };

    /// Global tile id at (col, row), or std::nullopt for an empty cell or an animation with no frames.
    std::optional<corundum::world::tilemap::TileId>
    resolve_tile_gid(const corundum::world::tilemap::TilemapLayer &layer,
                     const corundum::world::tilemap::Tilemap &tilemap, int col, int row, std::size_t cell_index,
                     const corundum::world::Scene &scene) {
      const uint32_t anim_idx = layer.baked_animation_index[cell_index];
      if (anim_idx == corundum::world::tilemap::TilemapLayer::k_no_animation) {
        const corundum::world::tilemap::TileId gid = tilemap.tile_at(layer, col, row);
        if (gid == corundum::world::tilemap::k_empty_tile)
          return std::nullopt;
        return gid;
      }

      const auto &anim = layer.baked_animations[anim_idx];
      if (anim.frame_gids.empty()) [[unlikely]]
        return std::nullopt;
      const auto frame_index =
          static_cast<std::size_t>(static_cast<int>(scene.elapsed_time * anim.fps)) % anim.frame_gids.size();
      return anim.frame_gids[frame_index];
    }

    ResolvedTile make_resolved_tile(const TileRenderParams &ctx, const corundum::world::tilemap::TilemapLayer &layer,
                                    const corundum::world::tilemap::TilemapTileset &tileset, int col, int row,
                                    std::size_t cell_index, corundum::world::tilemap::TileId gid, uint32_t texture_id,
                                    const corundum::core::GameConfig &cfg) {
      const auto source = corundum::world::tilemap::tile_source_rect(tileset, gid);
      const uint8_t flags = layer.baked_flip_flags[cell_index];
      const bool flip_x = (flags & corundum::world::tilemap::k_flip_h) != 0;
      const bool flip_y = (flags & corundum::world::tilemap::k_flip_v) != 0;
      const int elevation = (!layer.elevation.empty() && std::cmp_less(cell_index, layer.elevation.size()))
                                ? static_cast<int>(layer.elevation[cell_index])
                                : 0;

      const int abs_col = ctx.chunk_offset_col + col;
      const int abs_row = ctx.chunk_offset_row + row;
      const corundum::core::math::Vec2 world_pos = corundum::core::math::tile_to_world(
          static_cast<float>(abs_col), static_cast<float>(abs_row), elevation, ctx.iso);

      // Pivot is always measured against the full (untrimmed) frame, not the trimmed size, so sprites
      // sharing one canvas convention (e.g. a wall body and its separately-authored topper) stay
      // aligned to each other regardless of how much padding either one had trimmed away. Both pivot
      // and frame size are per-tile (TilesetInfo::tile_pivot_*/tile_full_*) since spritepacker computes
      // them per sprite — there's no tileset-wide default to fall back to.
      const int local_id = static_cast<int>(gid) - static_cast<int>(tileset.first_gid);
      const auto frame = corundum::world::tilemap::get_tile_frame_offset(tileset.info, local_id);
      const auto pivot = corundum::world::tilemap::get_tile_pivot(tileset.info, local_id);
      const float scaled_tw = static_cast<float>(frame.full_width) * cfg.tile_scale;
      const float scaled_th = static_cast<float>(frame.full_height) * cfg.tile_scale;
      const float trim_x_px = static_cast<float>(frame.trim_x) * cfg.tile_scale;
      const float trim_y_px = static_cast<float>(frame.trim_y) * cfg.tile_scale;
      const float depth =
          corundum::core::math::iso_depth_key(static_cast<float>(abs_col), static_cast<float>(abs_row),
                                              static_cast<float>(elevation), ctx.iso.half_th, ctx.iso.elev_step);

      return {
          .tex_id = texture_id,
          .src = source,
          // Anchor at the cell center (tile_to_world + half_th, since tile_to_world returns the cell's
          // top vertex under this codebase's TOP-vertex projection convention), then shift by the trim
          // rect's own offset within the full frame — only the trimmed region is drawn.
          .position =
              {
                  .x = (world_pos.x - (pivot.x * scaled_tw)) + trim_x_px,
                  .y = world_pos.y + ctx.iso.half_th - core::math::pivot_top_offset(pivot.y, scaled_th) + trim_y_px,
              },
          .scale = static_cast<float>(cfg.tile_scale),
          .flip_x = flip_x,
          .flip_y = flip_y,
          .depth = depth,
          .elevation = elevation,
      };
    }

    /// Resolves the tile at (col, row) in @p layer to draw data, or std::nullopt if the cell is empty/unrenderable.
    /// Shared by render_tile_layer (immediate draw, z>0 bands) and collect_tile_layer (depth-sorted, z==0 band).
    std::optional<ResolvedTile> resolve_tile_cell(const TileRenderParams &ctx,
                                                  const corundum::world::tilemap::TilemapLayer &layer,
                                                  const corundum::world::tilemap::Tilemap &tilemap, int col, int row,
                                                  const corundum::core::GameConfig &cfg,
                                                  const corundum::world::Scene &scene) {
      const int cell_idx = (row * tilemap.width) + col;
      const auto cell_index = static_cast<std::size_t>(cell_idx);
      const std::optional<corundum::world::tilemap::TileId> gid =
          resolve_tile_gid(layer, tilemap, col, row, cell_index, scene);
      if (!gid)
        return std::nullopt;

      const corundum::world::tilemap::TilemapTileset *tileset =
          corundum::world::tilemap::find_tileset(tilemap.tilesets, *gid);
      if (tileset == nullptr) [[unlikely]]
        return std::nullopt;

      const auto tileset_index = static_cast<std::size_t>(std::distance(tilemap.tilesets.data(), tileset));
      const uint32_t texture_id = (*ctx.tex_ids)[tileset_index];
      if (texture_id == 0) [[unlikely]]
        return std::nullopt;

      return make_resolved_tile(ctx, layer, *tileset, col, row, cell_index, *gid, texture_id, cfg);
    }

    /// Computes cull bounds for one tile layer, expanding the camera rect by per-side
    /// art-extent pads so tiles whose anchors fall outside the viewport but whose sprite
    /// art extends into it aren't skipped.
    core::math::IsometricCullBounds compute_layer_cull_bounds(const TileRenderParams &params,
                                                              const corundum::world::tilemap::TilemapLayer &layer,
                                                              const corundum::world::tilemap::Tilemap &tilemap,
                                                              float tile_scale) {
      if (params.viewport_w <= 0.f || params.viewport_h <= 0.f)
        return {
            .depth_min = std::numeric_limits<int>::min() / 2,
            .depth_max = std::numeric_limits<int>::max() / 2,
            .u_min = -1e30f,
            .u_max = 1e30f,
        };

      const float max_fw = static_cast<float>(tilemap.max_tile_full_w) * tile_scale;
      const float max_fh = static_cast<float>(tilemap.max_tile_full_h) * tile_scale;
      // Half the diamond height — matches resolve_tile_cell()'s anchor offset so cull padding tracks
      // where the sprite actually sits, not the cell's bottom vertex.
      const float cell_height = params.iso.half_th;
      const float elevation_pad = static_cast<float>(layer.max_elevation) * params.iso.elev_step;

      const float padded_left = params.camera_x - max_fw;
      const float padded_top = params.camera_y - (cell_height + max_fh);
      const float padded_right = params.camera_x + params.viewport_w + max_fw;
      const float padded_bottom = params.camera_y + params.viewport_h + max_fh - cell_height + elevation_pad;

      return core::math::compute_isometric_cull_bounds(padded_left, padded_top, padded_right, padded_bottom,
                                                       params.iso);
    }

    /// Calls @p visit with each renderable cell of @p layer inside the cull bounds, back to front
    /// (ascending col + row, then ascending col).
    /// @pre ctx.tilemap has at least one tileset.
    template <typename Visit>
    void for_each_visible_tile(const TileRenderParams &ctx, const corundum::world::tilemap::TilemapLayer &layer,
                               const core::GameConfig &cfg, const corundum::world::Scene &scene, Visit visit) {
      const corundum::world::tilemap::Tilemap &tilemap = *ctx.tilemap;
      const core::math::IsometricCullBounds cull = compute_layer_cull_bounds(ctx, layer, tilemap, cfg.tile_scale);
      const int offset_depth = ctx.chunk_offset_col + ctx.chunk_offset_row;
      const int depth_max = tilemap.width + tilemap.height - 2;
      for (int depth = std::max(0, cull.depth_min - offset_depth);
           depth <= std::min(depth_max, cull.depth_max - offset_depth); ++depth) {
        int first_col = std::max(0, depth - (tilemap.height - 1));
        int last_col = std::min(tilemap.width - 1, depth);
        first_col = std::max(first_col, core::math::isometric_cull_first_column(cull, depth + offset_depth) -
                                            ctx.chunk_offset_col);
        last_col = std::min(last_col,
                            core::math::isometric_cull_last_column(cull, depth + offset_depth) - ctx.chunk_offset_col);

        for (int col = first_col; col <= last_col; ++col) {
          const std::optional<ResolvedTile> tile = resolve_tile_cell(ctx, layer, tilemap, col, depth - col, cfg, scene);
          if (tile)
            visit(*tile);
        }
      }
    }

    platform::DrawSprite to_draw_sprite(const ResolvedTile &tile) noexcept {
      return {
          .texture_id = tile.tex_id,
          .position = tile.position,
          .source = tile.src,
          .scale = {.x = tile.scale, .y = tile.scale},
          .flip_x = tile.flip_x,
          .flip_y = tile.flip_y,
      };
    }

    render::DepthEntry to_depth_entry(const ResolvedTile &tile) noexcept {
      return {
          .tex_id = tile.tex_id,
          .src = tile.src,
          .x = tile.position.x,
          .y = tile.position.y,
          .depth = tile.depth,
          .scale = tile.scale,
          .flip_x = tile.flip_x,
          .flip_y = tile.flip_y,
      };
    }

    void render_tile_layer(corundum::platform::Renderer &r, const TileRenderParams &ctx, int z_index,
                           const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene) {
      const corundum::world::tilemap::Tilemap &tilemap = *ctx.tilemap;
      if (tilemap.tilesets.empty())
        return;

      for (const auto &layer : tilemap.layers) {
        if (!layer.visible || layer.z_index != z_index || layer.depth_sorted)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene, [&](const ResolvedTile &tile) { r.draw(to_draw_sprite(tile)); });
      }
    }

    /// Same iteration as render_tile_layer, but splits tiles by elevation: flat ground (elevation 0, the vast
    /// majority of tiles) draws immediately, unconditionally beneath entities — exactly the old behavior, so
    /// flat maps are visually unaffected. Only elevated tiles (elevation > 0) are collected into @p out to be
    /// depth-sorted against entities, since only those need occlusion interaction (see design checklist §2).
    /// A flat tile can never be taller than the diamond it occupies, so it can never occlude a taller entity
    /// sprite standing nearby; an elevated tile can, which is the whole point of this fix.
    void collect_tile_layer(corundum::platform::Renderer &r, const TileRenderParams &ctx, int z_index,
                            const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene,
                            std::vector<render::DepthEntry> &out) {
      const corundum::world::tilemap::Tilemap &tilemap = *ctx.tilemap;
      if (tilemap.tilesets.empty())
        return;

      for (const auto &layer : tilemap.layers) {
        if (!layer.visible || layer.z_index != z_index)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene, [&](const ResolvedTile &tile) {
          if (tile.elevation <= 0)
            r.draw(to_draw_sprite(tile));
          else
            out.push_back(to_depth_entry(tile));
        });
      }
    }

    /// Collects tiles from every z_index>0, depth_sorted==true layer into @p out, unconditionally —
    /// unlike collect_tile_layer's ground-layer elevation<=0 fast path, an overlay tile's occlusion
    /// potential comes from its art/pivot (a wall body taller than one cell), not its terrain-elevation
    /// value, so there is no "this tile can't occlude anything" case to skip. Every matching tile always
    /// goes into @p out to be depth-sorted against entities and elevated ground tiles.
    void collect_sorted_overlay_layers(const TileRenderParams &ctx, const corundum::core::GameConfig &cfg,
                                       const corundum::world::Scene &scene, std::vector<render::DepthEntry> &out) {
      const corundum::world::tilemap::Tilemap &tilemap = *ctx.tilemap;
      if (tilemap.tilesets.empty())
        return;

      for (const auto &layer : tilemap.layers) {
        if (!layer.visible || layer.z_index <= 0 || !layer.depth_sorted)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene,
                              [&](const ResolvedTile &tile) { out.push_back(to_depth_entry(tile)); });
      }
    }

    /// Collects z_index==0 tiles from the single-map tilemap into @p out. Mirrors render_tilemap's context setup.
    void collect_ground_tiles_map(corundum::platform::Renderer &r, const render::RenderState &state,
                                  const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene,
                                  std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                  int window_w, int window_h) {
      const auto &tilemap = state.map_data.tilemap;
      if (tilemap.tilesets.empty())
        return;

      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{
          .tilemap = &tilemap,
          .tex_ids = &state.map_data.tileset_texture_ids,
          .iso = iso,
          .camera_x = cam_x,
          .camera_y = cam_y,
          .viewport_w = vp_w,
          .viewport_h = vp_h,
          .chunk_offset_col = 0,
          .chunk_offset_row = 0,
      };
      collect_tile_layer(r, ctx, 0, cfg, scene, out);
    }

    /// Collects z_index==0 tiles from every active chunk into @p out. Mirrors render_chunk's context setup.
    void collect_ground_tiles_chunks(corundum::platform::Renderer &r, const render::RenderState &state,
                                     const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene,
                                     std::vector<render::DepthEntry> &out, float cam_x, float cam_y, float zoom,
                                     int window_w, int window_h) {
      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      for (const auto &chunk : state.chunks.active()) {
        const auto &tilemap = chunk.tilemap;
        if (tilemap.tilesets.empty())
          continue;

        const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                          : state.manifest.chunks_tall * state.manifest.chunk_size;
        const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                              cfg.tile_scale, cfg.elevation_step_px);
        const TileRenderParams ctx{
            .tilemap = &tilemap,
            .tex_ids = &chunk.tileset_texture_ids,
            .iso = iso,
            .camera_x = cam_x,
            .camera_y = cam_y,
            .viewport_w = vp_w,
            .viewport_h = vp_h,
            .chunk_offset_col = chunk.coord.col * state.manifest.chunk_size,
            .chunk_offset_row = chunk.coord.row * state.manifest.chunk_size,
        };
        collect_tile_layer(r, ctx, 0, cfg, scene, out);
      }
    }

    /// Collects depth_sorted overlay-layer tiles from the single-map tilemap into @p out. Mirrors
    /// collect_ground_tiles_map's context setup.
    void collect_sorted_overlay_map(const render::RenderState &state, const corundum::core::GameConfig &cfg,
                                    const corundum::world::Scene &scene, std::vector<render::DepthEntry> &out,
                                    float cam_x, float cam_y, float zoom, int window_w, int window_h) {
      const auto &tilemap = state.map_data.tilemap;
      if (tilemap.tilesets.empty())
        return;

      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{
          .tilemap = &tilemap,
          .tex_ids = &state.map_data.tileset_texture_ids,
          .iso = iso,
          .camera_x = cam_x,
          .camera_y = cam_y,
          .viewport_w = vp_w,
          .viewport_h = vp_h,
          .chunk_offset_col = 0,
          .chunk_offset_row = 0,
      };
      collect_sorted_overlay_layers(ctx, cfg, scene, out);
    }

    /// Collects depth_sorted overlay-layer tiles from every active chunk into @p out. Mirrors
    /// collect_ground_tiles_chunks's context setup.
    void collect_sorted_overlay_chunks(const render::RenderState &state, const corundum::core::GameConfig &cfg,
                                       const corundum::world::Scene &scene, std::vector<render::DepthEntry> &out,
                                       float cam_x, float cam_y, float zoom, int window_w, int window_h) {
      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      for (const auto &chunk : state.chunks.active()) {
        const auto &tilemap = chunk.tilemap;
        if (tilemap.tilesets.empty())
          continue;

        const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                          : state.manifest.chunks_tall * state.manifest.chunk_size;
        const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                              cfg.tile_scale, cfg.elevation_step_px);
        const TileRenderParams ctx{
            .tilemap = &tilemap,
            .tex_ids = &chunk.tileset_texture_ids,
            .iso = iso,
            .camera_x = cam_x,
            .camera_y = cam_y,
            .viewport_w = vp_w,
            .viewport_h = vp_h,
            .chunk_offset_col = chunk.coord.col * state.manifest.chunk_size,
            .chunk_offset_row = chunk.coord.row * state.manifest.chunk_size,
        };
        collect_sorted_overlay_layers(ctx, cfg, scene, out);
      }
    }

    void render_tilemap(corundum::platform::Renderer &r, const render::RenderState &state, int z_index,
                        const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene, float cam_x,
                        float cam_y, float zoom, int window_w, int window_h) {
      const auto &tilemap = state.map_data.tilemap;
      if (tilemap.tilesets.empty())
        return;

      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{
          .tilemap = &tilemap,
          .tex_ids = &state.map_data.tileset_texture_ids,
          .iso = iso,
          .camera_x = cam_x,
          .camera_y = cam_y,
          .viewport_w = vp_w,
          .viewport_h = vp_h,
          .chunk_offset_col = 0,
          .chunk_offset_row = 0,
      };
      render_tile_layer(r, ctx, z_index, cfg, scene);
    }

    void render_chunk(corundum::platform::Renderer &r, const render::RenderState &state,
                      const render::ChunkEntry &chunk, int z_index, const corundum::core::GameConfig &cfg,
                      const corundum::world::Scene &scene, float cam_x, float cam_y, float zoom, int window_w,
                      int window_h) {
      const auto &tilemap = chunk.tilemap;
      if (tilemap.tilesets.empty())
        return;

      const float vp_w = zoom > 0.f ? static_cast<float>(window_w) / zoom : 0.f;
      const float vp_h = zoom > 0.f ? static_cast<float>(window_h) / zoom : 0.f;
      const int total_h = state.manifest.tiles_tall > 0 ? state.manifest.tiles_tall
                                                        : state.manifest.chunks_tall * state.manifest.chunk_size;
      const auto iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_h,
                                                            cfg.tile_scale, cfg.elevation_step_px);
      const TileRenderParams ctx{
          .tilemap = &tilemap,
          .tex_ids = &chunk.tileset_texture_ids,
          .iso = iso,
          .camera_x = cam_x,
          .camera_y = cam_y,
          .viewport_w = vp_w,
          .viewport_h = vp_h,
          .chunk_offset_col = chunk.coord.col * state.manifest.chunk_size,
          .chunk_offset_row = chunk.coord.row * state.manifest.chunk_size,
      };
      render_tile_layer(r, ctx, z_index, cfg, scene);
    }

    /// Fallback half diamond height used when no tilemap is loaded yet (ISO diamond_h default for 32×32
    /// tiles at 1×).
    constexpr float k_default_half_th = 8.f;

    /// Projection for entity anchors: the first resident chunk's, else the single map's, else a flat
    /// default so an entity still draws before any tilemap loads.
    core::math::IsometricParams ground_isometric_params(const render::RenderState &state,
                                                        const corundum::core::GameConfig &cfg) noexcept {
      core::math::IsometricParams iso{};
      if (!state.chunks.active_empty() && !state.chunks.active_at(0).tilemap.tilesets.empty()) {
        const corundum::world::tilemap::Tilemap &tilemap = state.chunks.active_at(0).tilemap;
        iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(),
                                                   state.manifest.effective_tiles_tall(), cfg.tile_scale,
                                                   cfg.elevation_step_px);
      } else if (!state.map_data.tilemap.tilesets.empty()) {
        const corundum::world::tilemap::Tilemap &tilemap = state.map_data.tilemap;
        iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), tilemap.height,
                                                   cfg.tile_scale, cfg.elevation_step_px);
      }
      if (iso.half_th == 0.f)
        iso.half_th = k_default_half_th;
      return iso;
    }

    /// Appends every on-screen entity sprite to @p out.
    void collect_entities(const render::RenderState &state, const corundum::core::GameConfig &cfg,
                          const corundum::world::Scene &scene, float alpha, float cam_x, float cam_y, float zoom,
                          int win_w, int win_h, const core::math::IsometricParams &iso,
                          std::vector<render::DepthEntry> &out) {
      const float scale = cfg.character_scale;
      const auto &transforms = scene.world.transforms;
      const auto &sprites = scene.world.sprites;
      const auto entities = sprites.active_entities();
      const float viewport_right = cam_x + (static_cast<float>(win_w) / zoom);
      const float viewport_bottom = cam_y + (static_cast<float>(win_h) / zoom);

      assert(sprites.count <= decltype(scene.world.sprites)::k_max);
      for (std::uint32_t i = 0; i < sprites.count; ++i) {
        const auto entity = entities[i];
        if (!transforms.has(entity)) [[unlikely]]
          continue;

        const auto transform_slot = transforms.dense_index(entity);
        const core::math::Vec2 drawn = interpolated_tile_position(state, entity, transforms.col[transform_slot],
                                                                  transforms.row[transform_slot], alpha);
        const float col_f = drawn.x;
        const float row_f = drawn.y;

        const auto result = state.sprite_index.get(sprites.sprite_id[i], sprites.anim_id[i], sprites.frame_index[i]);
        if (!result) [[unlikely]]
          continue;

        const auto &entry = *result;
        const float elevation = elevation_under(state, col_f, row_f);
        // Entity anchor = cell center (top vertex + half_th), matching the cell-center
        // anchor convention used for tile sprites — keeps actors on the ground instead
        // of floating half_th above it.
        const auto [entity_x, entity_y] = core::math::tile_to_world_center(col_f, row_f, elevation, iso);
        const float sprite_x = entity_x - (static_cast<float>(entry.src.width) * scale * 0.5f);
        const float sprite_y = entity_y - (entry.walk_offset * static_cast<float>(entry.src.height) * scale);

        // AABB cull — skip entities entirely outside the visible viewport
        const float sprite_w = static_cast<float>(entry.src.width) * scale;
        const float sprite_h = static_cast<float>(entry.src.height) * scale;
        if (sprite_x + sprite_w < cam_x || sprite_x > viewport_right || sprite_y + sprite_h < cam_y ||
            sprite_y > viewport_bottom)
          continue;

        const float depth = corundum::core::math::iso_depth_key(col_f, row_f, elevation, iso.half_th, iso.elev_step);
        out.push_back(
            {.tex_id = entry.tex_id, .src = entry.src, .x = sprite_x, .y = sprite_y, .depth = depth, .scale = scale});
      }
    }

    /// Depth-sort and draw the collected ground tiles and entities.
    ///
    /// Sort indices, not the 40-byte DepthEntry structs: std::ranges::sort is introsort (in-place,
    /// no O(n) aux buffer), unlike stable_sort. Depth ties only occur for flat tiles on integer
    /// anti-diagonals — not for entities at fractional positions — so stability was barely exercised;
    /// an unstable sort on 4-byte indices is both cheaper to move and allocation-free. draw_order is
    /// reused across frames (resized, never freed) so this touches no per-frame heap allocation.
    void draw_depth_sorted(corundum::platform::Renderer &r, const std::vector<render::DepthEntry> &entries,
                           std::vector<uint32_t> &order) {
      order.resize(entries.size());
      std::ranges::iota(order, 0u);
      std::ranges::sort(order, {}, [&](uint32_t i) noexcept { return entries[i].depth; });

      for (const uint32_t index : order) {
        const auto &entry = entries[index];
        r.draw(corundum::platform::DrawSprite{
            .texture_id = entry.tex_id,
            .position = {.x = entry.x, .y = entry.y},
            .source = entry.src,
            .scale = {.x = entry.scale, .y = entry.scale},
            .flip_x = entry.flip_x,
            .flip_y = entry.flip_y,
        });
      }
    }

    /// Draws the z_index==0 tile band and all entities. Flat ground (elevation 0) draws immediately,
    /// unconditionally beneath entities — a flat tile can never be taller than its own diamond, so it can
    /// never legitimately occlude a taller entity sprite standing near it; this preserves the old two-pass
    /// behavior exactly for flat maps. Elevated tiles (elevation > 0) are depth-sorted together with entities
    /// (see iso_depth_key) so a raised platform correctly occludes/is-occluded-by nearby entities and tiles.
    /// z_index>0 layers remain a separate, subsequent immediate-draw pass (always above entities).
    void render_ground_layer(corundum::platform::Renderer &r, render::RenderState &state,
                             const corundum::core::GameConfig &cfg, const corundum::world::Scene &scene, float alpha,
                             float cam_x, float cam_y, float zoom, int win_w, int win_h) {
      const core::math::IsometricParams iso = ground_isometric_params(state, cfg);
      state.draw_list.clear();

      if (!state.chunks.active_empty())
        collect_ground_tiles_chunks(r, state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);
      else
        collect_ground_tiles_map(r, state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);

      if (!state.chunks.active_empty())
        collect_sorted_overlay_chunks(state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);
      else
        collect_sorted_overlay_map(state, cfg, scene, state.draw_list, cam_x, cam_y, zoom, win_w, win_h);

      collect_entities(state, cfg, scene, alpha, cam_x, cam_y, zoom, win_w, win_h, iso, state.draw_list);
      draw_depth_sorted(r, state.draw_list, state.draw_order);
    }

  } // namespace

} // namespace corundum::render
