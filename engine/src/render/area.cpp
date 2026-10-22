// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/area.hpp>

#include <corundum/core/content_source.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/world_aggregates.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace corundum::render {

  namespace {

    /// Drop the area's reference on every texture it holds. Nothing is destroyed until the next
    /// purge_unused_textures(), so a texture the next area also uses survives the swap.
    void release_area_textures(corundum::platform::Renderer &r, std::vector<uint32_t> &area_texture_ids) {
      for (const uint32_t id : area_texture_ids)
        r.release_texture(id);
      area_texture_ids.clear();
    }

    /// Record @p ids (just returned by load_texture) as area textures. The area keeps one reference
    /// per distinct texture: a repeat (two chunks sharing a tileset) hands its extra reference
    /// straight back. Id 0, a failed load, holds no reference.
    void adopt_area_textures(corundum::platform::Renderer &r, std::vector<uint32_t> &area_texture_ids,
                             std::span<const uint32_t> ids) {
      for (const uint32_t id : ids) {
        if (id == 0)
          continue;
        if (std::ranges::find(area_texture_ids, id) != area_texture_ids.end())
          r.release_texture(id);
        else
          area_texture_ids.push_back(id);
      }
    }

    /// A tilemap with its tileset textures (id 0 where a texture failed) and its bands above ground.
    struct TilemapAssets {
      std::vector<int> above_z{};

      corundum::world::tilemap::Tilemap tilemap{};

      std::vector<uint32_t> tileset_texture_ids{};
    };

    /// One id per tileset, in tileset order. A texture that fails to load is logged and gets id 0,
    /// which the tile pass skips: missing art draws nothing rather than failing the area.
    std::vector<uint32_t> load_tileset_textures(corundum::platform::Renderer &r,
                                                const corundum::world::tilemap::Tilemap &tilemap) {
      std::vector<uint32_t> ids;
      ids.reserve(tilemap.tilesets.size());
      for (const auto &tileset : tilemap.tilesets) {
        const auto loaded = r.load_texture(tileset.info.path);
        if (!loaded)
          corundum::detail::warn_log("[renderer] WARNING: could not load texture '{}'", tileset.info.path);
        ids.push_back(loaded.value_or(0));
      }
      return ids;
    }

    /// Sorted, deduplicated z_index values above the ground band (z_index > 0).
    std::vector<int> collect_above_ground_bands(const corundum::world::tilemap::Tilemap &tilemap) {
      std::vector<int> bands;
      for (const auto &layer : tilemap.layers)
        if (layer.z_index > 0)
          bands.push_back(layer.z_index);
      std::ranges::sort(bands);
      bands.erase(std::ranges::unique(bands).begin(), bands.end());
      return bands;
    }

    std::expected<TilemapAssets, std::string> load_tilemap_assets(const corundum::core::ContentSource &content,
                                                                  corundum::platform::Renderer &r,
                                                                  const std::filesystem::path &path) {
      auto tilemap = corundum::world::tilemap::load_tilemap(content, path);
      if (!tilemap)
        return std::unexpected(std::move(tilemap).error());
      TilemapAssets assets{
          .above_z = collect_above_ground_bands(*tilemap),
          .tileset_texture_ids = load_tileset_textures(r, *tilemap),
      };
      assets.tilemap = std::move(*tilemap);
      return assets;
    }

    std::expected<render::ChunkEntry, std::string>
    load_chunk_entry(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                     const corundum::world::tilemap::WorldManifest &manifest, corundum::world::tilemap::ChunkCoord c,
                     const corundum::core::GameConfig &cfg) {
      const std::filesystem::path chunk_path = manifest.chunk_path(c);
      auto assets = load_tilemap_assets(content, r, chunk_path);
      if (!assets)
        return std::unexpected(std::move(assets).error());

      std::vector<corundum::world::Portal> portals;
      {
        const std::string stem = chunk_path.stem().string();
        const auto portals_path = std::filesystem::path(cfg.paths.portals_dir) / (stem + ".json");
        auto result = corundum::world::load_portals(content, portals_path);
        if (result.has_value())
          portals = std::move(*result);
        else
          corundum::detail::warn_log("[engine] WARN: portals for chunk {} skipped: {}", stem, result.error());
      }

      return render::ChunkEntry{
          .coord = c,
          .tilemap = std::move(assets->tilemap),
          .tileset_texture_ids = std::move(assets->tileset_texture_ids),
          .above_z = std::move(assets->above_z),
          .portals = std::move(portals),
      };
    }

    /// Fraction of a chunk the player must cross before the streaming window recenters.
    /// Relative to chunk_size so the margin scales with the world (2.56 tiles for a 128-tile
    /// chunk, 0.16 for an 8-tile one) rather than being tuned for a single chunk size.
    constexpr float k_recenter_margin_fraction = 0.02f;

    struct PlayerCenter {
      corundum::world::tilemap::ChunkCoord chunk{};

      float col{};

      float row{};
    };

    /// Where the player stands and which chunk contains that position.
    PlayerCenter player_center(const WorldArea &area, const corundum::core::GameConfig &cfg,
                               const corundum::world::Scene &scene) {
      using namespace corundum::world::tilemap;
      const auto iso = isometric_params(area, cfg);
      const auto pos_slot = scene.world.transforms.dense_index(scene.player);
      const float col = scene.world.transforms.col[pos_slot];
      const float row = scene.world.transforms.row[pos_slot];
      const auto [world_x, world_y] = core::math::tile_to_world(col, row, 0, iso);
      return {.chunk = chunk_at_iso(world_x, world_y, area.manifest, iso), .col = col, .row = row};
    }

    /// Recenter the window once the player has crossed far enough into the new center chunk.
    void recenter_window(WorldArea &area, const PlayerCenter &player) {
      const corundum::world::tilemap::ChunkCoord last = area.chunks.last_center();
      if (player.chunk == last)
        return;

      const float chunk_tiles = static_cast<float>(area.manifest.chunk_size);
      const float margin_tiles = k_recenter_margin_fraction * chunk_tiles;
      const float local_col = player.col - static_cast<float>(player.chunk.col * area.manifest.chunk_size);
      const float local_row = player.row - static_cast<float>(player.chunk.row * area.manifest.chunk_size);
      const bool x_ok =
          player.chunk.col == last.col || (local_col >= margin_tiles && local_col <= chunk_tiles - margin_tiles);
      const bool y_ok =
          player.chunk.row == last.row || (local_row >= margin_tiles && local_row <= chunk_tiles - margin_tiles);
      if (x_ok && y_ok)
        area.chunks.set_last_center(player.chunk);
    }

    /// Chunks the current window should hold, in row-major order.
    struct DesiredChunks {
      std::array<corundum::world::tilemap::ChunkCoord, render::ChunkWindow::k_max_active_chunks> coords{};

      std::size_t count{};

      [[nodiscard]] std::span<const corundum::world::tilemap::ChunkCoord> view() const noexcept {
        return {coords.data(), count};
      }
    };

    DesiredChunks desired_chunks(const WorldArea &area) {
      using namespace corundum::world::tilemap;
      DesiredChunks desired;
      for (int dy = -render::ChunkWindow::k_radius; dy <= render::ChunkWindow::k_radius; ++dy) {
        for (int dx = -render::ChunkWindow::k_radius; dx <= render::ChunkWindow::k_radius; ++dx) {
          const ChunkCoord c{.col = area.chunks.last_center().col + dx, .row = area.chunks.last_center().row + dy};
          if (area.manifest.in_bounds(c))
            desired.coords[desired.count++] = c;
        }
      }
      return desired;
    }

    /// Recenter the window on the player, drop chunks and pending loads it has left, and queue
    /// newly needed chunks. @return true if any active chunk was removed. Does not rebuild the
    /// aggregates.
    bool sync_active_chunks(WorldArea &area, const corundum::core::GameConfig &cfg,
                            const corundum::world::Scene &scene) {
      if (area.chunks.active_empty())
        return false;

      // A despawned player means no one to recenter on; leave the window where it is.
      if (!corundum::world::player_present(scene))
        return false;

      recenter_window(area, player_center(area, cfg, scene));

      const DesiredChunks desired = desired_chunks(area);
      const auto is_desired = [&](corundum::world::tilemap::ChunkCoord c) {
        return std::ranges::find(desired.view(), c) != desired.view().end();
      };
      const bool any_stale = area.chunks.prune_active([&](const render::ChunkEntry &e) { return is_desired(e.coord); });
      area.chunks.prune_pending(is_desired);

      for (const corundum::world::tilemap::ChunkCoord c : desired.view())
        if (!area.chunks.has(c))
          area.chunks.enqueue_pending(c);

      return any_stale;
    }

    /// Load the front pending chunk into the active window. A chunk that fails to load is logged
    /// and marked failed so it is never retried. @return true if a chunk became active.
    bool load_one_pending_chunk(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                                std::vector<uint32_t> &area_texture_ids, WorldArea &area,
                                const corundum::core::GameConfig &cfg) {
      const std::optional<corundum::world::tilemap::ChunkCoord> coord = area.chunks.pop_pending();
      if (!coord)
        return false;
      auto entry = load_chunk_entry(content, r, area.manifest, *coord, cfg);
      if (!entry) {
        corundum::detail::warn_log("[engine] WARN: chunk ({}, {}) failed to load: {}", coord->col, coord->row,
                                   entry.error());
        area.chunks.mark_failed(*coord);
        return false;
      }
      corundum::detail::info_log("[engine] Loaded chunk ({}, {})", coord->col, coord->row);
      adopt_area_textures(r, area_texture_ids, entry->tileset_texture_ids);
      area.chunks.add_active(std::move(*entry));
      return true;
    }

    struct SpawnPlacement {
      corundum::world::tilemap::ChunkCoord center{};

      float tile_col{};

      float tile_row{};
    };

    /// Default spawn is the manifest geometric centre. A supplied spawn (e.g. an interior
    /// exit portal back into the overworld) overrides it and re-centres the streaming window
    /// on that tile's chunk. Each coordinate independently falls back to the centre so a
    /// half-specified params (only one of spawn_col/spawn_row) is not silently ignored.
    SpawnPlacement spawn_placement(const corundum::world::tilemap::WorldManifest &manifest,
                                   const render::WorldLoadParams &params) {
      const int chunk_size = manifest.chunk_size;
      const int center_col = manifest.chunks_wide * chunk_size / 2;
      const int center_row = manifest.chunks_tall * chunk_size / 2;
      const float tile_col = params.spawn_col.value_or(static_cast<float>(center_col));
      const float tile_row = params.spawn_row.value_or(static_cast<float>(center_row));
      return {
          .center = {.col = static_cast<int>(tile_col) / chunk_size, .row = static_cast<int>(tile_row) / chunk_size},
          .tile_col = tile_col,
          .tile_row = tile_row,
      };
    }

    /// Load every chunk in the initial streaming window. Failed chunks are logged and marked
    /// failed so they are never retried.
    void load_initial_chunks(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                             std::vector<uint32_t> &area_texture_ids, WorldArea &area,
                             const corundum::core::GameConfig &cfg) {
      using namespace corundum::world::tilemap;
      for (const ChunkCoord c :
           active_chunk_coords(area.chunks.last_center(), render::ChunkWindow::k_radius, area.manifest)) {
        auto entry = load_chunk_entry(content, r, area.manifest, c, cfg);
        if (!entry) {
          corundum::detail::warn_log("[engine] WARN: chunk ({}, {}) skipped: {}", c.col, c.row, entry.error());
          area.chunks.mark_failed(c);
          continue;
        }
        adopt_area_textures(r, area_texture_ids, entry->tileset_texture_ids);
        area.chunks.add_active(std::move(*entry));
      }
    }

    WorldLoadInfo make_world_load_info(const WorldArea &area, const corundum::core::GameConfig &cfg,
                                       const SpawnPlacement &spawn) {
      const auto iso = isometric_params(area, cfg);
      const core::math::Vec2 spawn_pos{.x = spawn.tile_col, .y = spawn.tile_row};
      corundum::detail::info_log("[engine] World ready — spawn at tile ({:.0f}, {:.0f})", spawn_pos.x, spawn_pos.y);
      return {
          .half_tw = iso.half_tw,
          .half_th = iso.half_th,
          .x_origin = iso.x_origin,
          .spawn_world_pos = spawn_pos,
      };
    }

  } // namespace

  void unload_world(corundum::platform::Renderer &r, render::RenderState &state) {
    release_area_textures(r, state.area_texture_ids);
    state.area = std::monostate{};
    state.draw_list.clear();
    state.draw_order.clear();
    r.purge_unused_textures();
  }

  std::expected<void, std::string> load_map(const corundum::core::ContentSource &content,
                                            corundum::platform::Renderer &r, render::RenderState &state,
                                            const std::string &tilemap_path, const corundum::core::GameConfig &cfg) {
    release_area_textures(r, state.area_texture_ids);
    auto &area = state.area.emplace<SingleMapArea>();

    auto assets = load_tilemap_assets(content, r, tilemap_path);
    if (!assets)
      return std::unexpected(std::move(assets).error());
    adopt_area_textures(r, state.area_texture_ids, assets->tileset_texture_ids);

    const std::string stem = std::filesystem::path(tilemap_path).stem().string();
    const auto portals_file = std::format("{}/{}.json", cfg.paths.portals_dir, stem);
    auto portals = corundum::world::load_portals(content, portals_file);
    if (!portals)
      return std::unexpected(portals.error());
    if (!content.contains(portals_file))
      corundum::detail::info_log("[engine] 0 portals (no portals file at '{}')", portals_file);
    else
      corundum::detail::info_log("[engine] Loaded {} portals from '{}'", portals->size(), portals_file);

    area.walkability =
        corundum::world::tilemap::build_walkability_graph(assets->tilemap, static_cast<int>(cfg.max_step_height));
    area.map = {
        .tilemap = std::move(assets->tilemap),
        .tileset_texture_ids = std::move(assets->tileset_texture_ids),
        .above_z = std::move(assets->above_z),
        .portals = std::move(*portals),
    };

    // Upper bound is known at load time (map dims + entity cap), so this is the only
    // reserve draw_list ever needs — steady-state rendering never reallocates it.
    state.draw_list.reserve((static_cast<std::size_t>(area.map.tilemap.width) * area.map.tilemap.height) +
                            corundum::entities::k_max_entities);
    r.purge_unused_textures();
    return {};
  }

  std::expected<WorldLoadInfo, std::string> load_world(const corundum::core::ContentSource &content,
                                                       corundum::platform::Renderer &r, render::RenderState &state,
                                                       const corundum::core::GameConfig &cfg,
                                                       const WorldLoadParams &params) {
    release_area_textures(r, state.area_texture_ids);
    auto &area = state.area.emplace<WorldArea>();

    auto manifest = corundum::world::tilemap::load_world_manifest(content, cfg.paths.world_manifest_path);
    if (!manifest)
      return std::unexpected(manifest.error());
    area.manifest = std::move(*manifest);
    corundum::detail::info_log("[engine] World manifest: {}×{} chunks of {}×{} tiles", area.manifest.chunks_wide,
                               area.manifest.chunks_tall, area.manifest.chunk_size, area.manifest.chunk_size);

    // Chunk streaming keeps a fixed ChunkWindow::k_side × k_side window, so this bound holds
    // for the life of the world regardless of which chunks are currently active.
    state.draw_list.reserve((render::ChunkWindow::k_max_active_chunks *
                             static_cast<std::size_t>(area.manifest.chunk_size) * area.manifest.chunk_size) +
                            corundum::entities::k_max_entities);

    const SpawnPlacement spawn = spawn_placement(area.manifest, params);
    area.chunks.set_last_center(spawn.center);
    load_initial_chunks(content, r, state.area_texture_ids, area, cfg);
    if (area.chunks.active_empty())
      return std::unexpected(std::format("world '{}' loaded no chunks", cfg.paths.world_manifest_path));
    rebuild_world_aggregates(area, static_cast<int>(cfg.max_step_height));

    const WorldLoadInfo info = make_world_load_info(area, cfg, spawn);
    r.purge_unused_textures();
    return info;
  }

  void stream_world_chunks(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                           render::RenderState &state, const corundum::core::GameConfig &cfg,
                           const corundum::world::Scene &scene) {
    auto *world_area = std::get_if<WorldArea>(&state.area);
    if (world_area == nullptr || world_area->chunks.active_empty())
      return;
    const bool pruned = sync_active_chunks(*world_area, cfg, scene);
    const bool loaded = load_one_pending_chunk(content, r, state.area_texture_ids, *world_area, cfg);
    if (pruned || loaded)
      rebuild_world_aggregates(*world_area, static_cast<int>(cfg.max_step_height));
  }

} // namespace corundum::render
