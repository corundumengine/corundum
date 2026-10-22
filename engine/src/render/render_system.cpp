// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/render_system.hpp>

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/verify.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/world_aggregates.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <algorithm>
#include <array>
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
#include <variant>
#include <vector>

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
    StepSnapshot &snapshot = state.previous_step;
    for (std::uint32_t slot = 0; slot < transforms.count; ++slot) {
      const corundum::entities::EntityId e = entities[slot];
      snapshot.col[e.index] = transforms.col[slot];
      snapshot.row[e.index] = transforms.row[slot];
      snapshot.generation[e.index] = e.generation;
    }
    snapshot.camera = {.x = scene.camera.x, .y = scene.camera.y};
    snapshot.zoom = scene.camera.zoom;
  }

  core::math::Vec2 interpolated_tile_position(const render::RenderState &state, corundum::entities::EntityId e,
                                              float col, float row, float alpha) noexcept {
    const StepSnapshot &snapshot = state.previous_step;
    if (e.index >= corundum::entities::k_max_entities || snapshot.generation[e.index] != e.generation)
      return {.x = col, .y = row};
    return {
        .x = std::lerp(snapshot.col[e.index], col, alpha),
        .y = std::lerp(snapshot.row[e.index], row, alpha),
    };
  }

  namespace {

    /// Fallback half diamond height used when no tilemap is loaded yet (ISO diamond_h default for 32×32
    /// tiles at 1×).
    constexpr float k_default_half_th = 8.f;

    /// The interpolated camera for one frame and the viewport it sees.
    struct FrameView {
      core::math::Vec2 camera{}; ///< Top-left, world pixels.

      core::math::Vec2 viewport_screen{}; ///< Window size, screen pixels.

      /// Window size in world pixels; {0, 0} when zoom <= 0, which disables tile culling.
      core::math::Vec2 viewport_world{};

      float zoom{1.f};
    };

    /// One tilemap to draw this frame (the single map, or one resident chunk) with its projection.
    struct TileRenderParams {
      const world::tilemap::Tilemap *tilemap{nullptr};

      std::span<const uint32_t> texture_ids{};

      core::math::IsometricParams iso{};

      core::math::Vec2 camera{}; ///< Top-left, world pixels (interpolated).

      core::math::Vec2 viewport_world{}; ///< Viewport size, world pixels.

      int chunk_offset_col{0};

      int chunk_offset_row{0};
    };

    /// This frame's tile sources in draw order: at most one per resident chunk, so the fixed array
    /// never allocates.
    struct TileSources {
      std::array<TileRenderParams, ChunkWindow::k_max_active_chunks> items{};

      std::size_t count{0};

      [[nodiscard]] std::span<const TileRenderParams> view() const noexcept { return {items.data(), count}; }
    };

    /// One resolved tile: its sprite, ready to draw or depth-sort, and its raw elevation.
    struct ResolvedTile {
      DepthEntry sprite{};

      int elevation{0}; ///< Raw elevation [0-255]; 0 for flat ground.
    };

    FrameView make_frame_view(const RenderState &state, const world::Scene &scene, float alpha, int win_w,
                              int win_h) noexcept {
      const StepSnapshot &snapshot = state.previous_step;
      const float zoom = std::lerp(snapshot.zoom, scene.camera.zoom, alpha);
      const core::math::Vec2 viewport_screen{.x = static_cast<float>(win_w), .y = static_cast<float>(win_h)};
      core::math::Vec2 viewport_world{};
      if (zoom > 0.f)
        viewport_world = {.x = viewport_screen.x / zoom, .y = viewport_screen.y / zoom};
      const core::math::Vec2 camera{
          .x = std::lerp(snapshot.camera.x, scene.camera.x, alpha),
          .y = std::lerp(snapshot.camera.y, scene.camera.y, alpha),
      };
      return {
          .camera = camera,
          .viewport_screen = viewport_screen,
          .viewport_world = viewport_world,
          .zoom = zoom,
      };
    }

    TileRenderParams make_tile_params(const world::tilemap::Tilemap &tilemap, std::span<const uint32_t> texture_ids,
                                      int total_tiles_tall, world::tilemap::ChunkCoord offset_tiles,
                                      const core::GameConfig &cfg, const FrameView &view) noexcept {
      return {
          .tilemap = &tilemap,
          .texture_ids = texture_ids,
          .iso = core::math::compute_isometric_params(tilemap.diamond_w(), tilemap.diamond_h(), total_tiles_tall,
                                                      cfg.tile_scale, cfg.elevation_step_px),
          .camera = view.camera,
          .viewport_world = view.viewport_world,
          .chunk_offset_col = offset_tiles.col,
          .chunk_offset_row = offset_tiles.row,
      };
    }

    /// Every tilemap that has tilesets: each active chunk in draw order, else the single map.
    TileSources collect_tile_sources(const RenderState &state, const core::GameConfig &cfg,
                                     const FrameView &view) noexcept {
      TileSources sources;
      if (const auto *world_area = std::get_if<WorldArea>(&state.area)) {
        const world::tilemap::WorldManifest &manifest = world_area->manifest;
        for (const ChunkEntry &chunk : world_area->chunks.active()) {
          if (chunk.tilemap.tilesets.empty())
            continue;
          // Overflowing the fixed array would write past it; ChunkWindow bounds the active set.
          core::verify(sources.count < sources.items.size(), "render: more active chunks than the window holds");
          const world::tilemap::ChunkCoord offset{
              .col = chunk.coord.col * manifest.chunk_size,
              .row = chunk.coord.row * manifest.chunk_size,
          };
          sources.items[sources.count++] = make_tile_params(chunk.tilemap, chunk.tileset_texture_ids,
                                                            manifest.effective_tiles_tall(), offset, cfg, view);
        }
        return sources;
      }
      const auto *single = std::get_if<SingleMapArea>(&state.area);
      if (single != nullptr && !single->map.tilemap.tilesets.empty()) {
        const world::tilemap::ChunkCoord no_offset{};
        sources.items[sources.count++] = make_tile_params(single->map.tilemap, single->map.tileset_texture_ids,
                                                          single->map.tilemap.height, no_offset, cfg, view);
      }
      return sources;
    }

    platform::DrawSprite to_draw_sprite(const DepthEntry &entry) noexcept {
      return {
          .texture_id = entry.tex_id,
          .position = {.x = entry.x, .y = entry.y},
          .source = entry.src,
          .scale = {.x = entry.scale, .y = entry.scale},
          .flip_x = entry.flip_x,
          .flip_y = entry.flip_y,
      };
    }

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
          .sprite =
              {
                  .tex_id = texture_id,
                  .src = source,
                  // Anchor at the cell center (tile_to_world + half_th, since tile_to_world returns the cell's
                  // top vertex under this codebase's TOP-vertex projection convention), then shift by the trim
                  // rect's own offset within the full frame — only the trimmed region is drawn.
                  .x = (world_pos.x - (pivot.x * scaled_tw)) + trim_x_px,
                  .y = world_pos.y + ctx.iso.half_th - core::math::pivot_top_offset(pivot.y, scaled_th) + trim_y_px,
                  .depth = depth,
                  .scale = static_cast<float>(cfg.tile_scale),
                  .flip_x = flip_x,
                  .flip_y = flip_y,
              },
          .elevation = elevation,
      };
    }

    /// Resolves the tile at (col, row) in @p layer to draw data, or std::nullopt if the cell is empty/unrenderable.
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
      const uint32_t texture_id = ctx.texture_ids[tileset_index];
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
      if (params.viewport_world.x <= 0.f || params.viewport_world.y <= 0.f)
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

      const float padded_left = params.camera.x - max_fw;
      const float padded_top = params.camera.y - (cell_height + max_fh);
      const float padded_right = params.camera.x + params.viewport_world.x + max_fw;
      const float padded_bottom = params.camera.y + params.viewport_world.y + max_fh - cell_height + elevation_pad;

      return core::math::compute_isometric_cull_bounds(padded_left, padded_top, padded_right, padded_bottom,
                                                       params.iso);
    }

    /// Calls @p visit with each renderable cell of @p layer inside the cull bounds, back to front
    /// (ascending col + row, then ascending col).
    /// @pre ctx.tilemap has at least one tileset (collect_tile_sources() guarantees it).
    template <typename Visit>
    void for_each_visible_tile(const TileRenderParams &ctx, const world::tilemap::TilemapLayer &layer,
                               const core::GameConfig &cfg, const world::Scene &scene, const Visit &visit) {
      const world::tilemap::Tilemap &tilemap = *ctx.tilemap;
      const core::math::IsometricCullBounds cull = compute_layer_cull_bounds(ctx, layer, tilemap, cfg.tile_scale);
      const int offset_depth = ctx.chunk_offset_col + ctx.chunk_offset_row;
      const int first_depth = std::max(0, cull.depth_min - offset_depth);
      const int last_depth = std::min(tilemap.width + tilemap.height - 2, cull.depth_max - offset_depth);
      for (int depth = first_depth; depth <= last_depth; ++depth) {
        const int cull_first_col = core::math::isometric_cull_first_column(cull, depth + offset_depth);
        const int cull_last_col = core::math::isometric_cull_last_column(cull, depth + offset_depth);
        const int first_col = std::max({0, depth - (tilemap.height - 1), cull_first_col - ctx.chunk_offset_col});
        const int last_col = std::min({tilemap.width - 1, depth, cull_last_col - ctx.chunk_offset_col});
        for (int col = first_col; col <= last_col; ++col) {
          if (const std::optional<ResolvedTile> tile =
                  resolve_tile_cell(ctx, layer, tilemap, col, depth - col, cfg, scene))
            visit(*tile);
        }
      }
    }

    /// Flat ground (elevation 0, the vast majority of tiles) draws immediately, unconditionally beneath
    /// entities — exactly the old behavior, so flat maps are visually unaffected. Only elevated tiles
    /// (elevation > 0) are collected into @p sorted to be depth-sorted against entities, since only those
    /// need occlusion interaction (see design checklist §2). A flat tile can never be taller than the
    /// diamond it occupies, so it can never occlude a taller entity sprite standing nearby; an elevated
    /// tile can, which is the whole point of this fix.
    void draw_ground_band(platform::Renderer &r, const TileRenderParams &ctx, const core::GameConfig &cfg,
                          const world::Scene &scene, std::vector<DepthEntry> &sorted) {
      for (const auto &layer : ctx.tilemap->layers) {
        if (!layer.visible || layer.z_index != 0)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene, [&](const ResolvedTile &tile) {
          if (tile.elevation <= 0)
            r.draw(to_draw_sprite(tile.sprite));
          else
            sorted.push_back(tile.sprite);
        });
      }
    }

    /// Collects tiles from every z_index>0, depth_sorted==true layer into @p sorted, unconditionally —
    /// unlike draw_ground_band's elevation<=0 fast path, an overlay tile's occlusion potential comes
    /// from its art/pivot (a wall body taller than one cell), not its terrain-elevation value, so there
    /// is no "this tile can't occlude anything" case to skip. Every matching tile always goes into
    /// @p sorted to be depth-sorted against entities and elevated ground tiles.
    void collect_sorted_overlays(const TileRenderParams &ctx, const core::GameConfig &cfg, const world::Scene &scene,
                                 std::vector<DepthEntry> &sorted) {
      for (const auto &layer : ctx.tilemap->layers) {
        if (!layer.visible || layer.z_index <= 0 || !layer.depth_sorted)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene, [&](const ResolvedTile &tile) { sorted.push_back(tile.sprite); });
      }
    }

    /// Draws the visible layers of band @p z_index that are not depth-sorted, immediately, above
    /// everything in the ground band.
    void draw_overlay_band(platform::Renderer &r, const TileRenderParams &ctx, int z_index, const core::GameConfig &cfg,
                           const world::Scene &scene) {
      for (const auto &layer : ctx.tilemap->layers) {
        if (!layer.visible || layer.z_index != z_index || layer.depth_sorted)
          continue;
        for_each_visible_tile(ctx, layer, cfg, scene,
                              [&](const ResolvedTile &tile) { r.draw(to_draw_sprite(tile.sprite)); });
      }
    }

    /// Projection for entity anchors: the first resident chunk's, else the single map's, else a flat
    /// default so an entity still draws before any tilemap loads.
    core::math::IsometricParams entity_isometric_params(const RenderState &state,
                                                        const core::GameConfig &cfg) noexcept {
      core::math::IsometricParams iso{};
      const auto *world_area = std::get_if<WorldArea>(&state.area);
      const auto *single = std::get_if<SingleMapArea>(&state.area);
      const bool world_has_tilesets = world_area != nullptr && !world_area->chunks.active_empty() &&
                                      !world_area->chunks.active_at(0).tilemap.tilesets.empty();
      if (world_has_tilesets)
        iso = isometric_params(*world_area, cfg);
      else if (single != nullptr && !single->map.tilemap.tilesets.empty())
        iso = isometric_params(*single, cfg);
      if (iso.half_th == 0.f)
        iso.half_th = k_default_half_th;
      return iso;
    }

    /// Appends every on-screen entity sprite to @p sorted.
    void collect_entities(const RenderState &state, const core::GameConfig &cfg, const world::Scene &scene, float alpha,
                          const FrameView &view, std::vector<DepthEntry> &sorted) {
      const float scale = cfg.character_scale;
      const auto &transforms = scene.world.transforms;
      const auto &sprites = scene.world.sprites;
      const auto entities = sprites.active_entities();
      const float viewport_right = view.camera.x + (view.viewport_screen.x / view.zoom);
      const float viewport_bottom = view.camera.y + (view.viewport_screen.y / view.zoom);
      const core::math::IsometricParams iso = entity_isometric_params(state, cfg);

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
        if (sprite_x + sprite_w < view.camera.x || sprite_x > viewport_right || sprite_y + sprite_h < view.camera.y ||
            sprite_y > viewport_bottom)
          continue;

        const float depth = corundum::core::math::iso_depth_key(col_f, row_f, elevation, iso.half_th, iso.elev_step);
        sorted.push_back({
            .tex_id = entry.tex_id,
            .src = entry.src,
            .x = sprite_x,
            .y = sprite_y,
            .depth = depth,
            .scale = scale,
        });
      }
    }

    /// Depth-sort and draw the collected ground tiles and entities.
    ///
    /// Sort indices, not the 40-byte DepthEntry structs: std::ranges::sort is introsort (in-place,
    /// no O(n) aux buffer), unlike stable_sort. Depth ties only occur for flat tiles on integer
    /// anti-diagonals — not for entities at fractional positions — so stability was barely exercised;
    /// an unstable sort on 4-byte indices is both cheaper to move and allocation-free. draw_order is
    /// reused across frames (resized, never freed) so this touches no per-frame heap allocation.
    void draw_depth_sorted(platform::Renderer &r, const std::vector<DepthEntry> &entries,
                           std::vector<uint32_t> &order) {
      order.resize(entries.size());
      std::ranges::iota(order, 0u);
      std::ranges::sort(order, {}, [&](uint32_t i) noexcept { return entries[i].depth; });
      for (const uint32_t index : order)
        r.draw(to_draw_sprite(entries[index]));
    }

    /// Draws the z_index==0 tile band and all entities. Flat ground (elevation 0) draws immediately,
    /// unconditionally beneath entities — a flat tile can never be taller than its own diamond, so it can
    /// never legitimately occlude a taller entity sprite standing near it; this preserves the old two-pass
    /// behavior exactly for flat maps. Elevated tiles (elevation > 0) are depth-sorted together with entities
    /// (see iso_depth_key) so a raised platform correctly occludes/is-occluded-by nearby entities and tiles.
    /// z_index>0 layers remain a separate, subsequent immediate-draw pass (always above entities).
    void draw_ground_and_entities(platform::Renderer &r, RenderState &state, const core::GameConfig &cfg,
                                  const world::Scene &scene, float alpha, const FrameView &view,
                                  std::span<const TileRenderParams> sources) {
      state.draw_list.clear();
      for (const TileRenderParams &source : sources)
        draw_ground_band(r, source, cfg, scene, state.draw_list);
      for (const TileRenderParams &source : sources)
        collect_sorted_overlays(source, cfg, scene, state.draw_list);
      collect_entities(state, cfg, scene, alpha, view, state.draw_list);
      draw_depth_sorted(r, state.draw_list, state.draw_order);
    }

    /// The z_index > 0 bands to draw over the ground band, ascending. In World mode, first rebuilds
    /// the cached union of the active chunks' bands when the active set changed.
    std::span<const int> refresh_above_ground_bands(RenderState &state) {
      if (const auto *single = std::get_if<SingleMapArea>(&state.area))
        return single->map.above_z;
      auto *world_area = std::get_if<WorldArea>(&state.area);
      if (world_area == nullptr)
        return {};
      if (world_area->chunks.consume_dirty()) {
        std::vector<int> &bands = world_area->above_z_cache;
        bands.clear();
        for (const ChunkEntry &chunk : world_area->chunks.active())
          bands.insert(bands.end(), chunk.above_z.begin(), chunk.above_z.end());
        std::ranges::sort(bands);
        bands.erase(std::ranges::unique(bands).begin(), bands.end());
      }
      return world_area->above_z_cache;
    }

  } // namespace

  void render(platform::Renderer &r, RenderState &state, const core::GameConfig &cfg, const world::Scene &scene,
              float alpha, int win_w, int win_h) {
    const FrameView view = make_frame_view(state, scene, alpha, win_w, win_h);
    const TileSources sources = collect_tile_sources(state, cfg, view);

    r.set_world_view(view.camera, view.viewport_screen, view.zoom);
    draw_ground_and_entities(r, state, cfg, scene, alpha, view, sources.view());

    for (const int z_index : refresh_above_ground_bands(state)) {
      r.set_world_view(view.camera, view.viewport_screen, view.zoom);
      for (const TileRenderParams &source : sources.view())
        draw_overlay_band(r, source, z_index, cfg, scene);
    }
    r.reset_screen_view();
  }

} // namespace corundum::render
