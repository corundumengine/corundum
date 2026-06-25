#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/render/data/render_state.hpp>
#include <corundum/render/sys/render_sys.hpp>

namespace corundum::gameplay::world {

  [[nodiscard]] MapView build_map_view(const render::data::RenderState &render, const core::GameConfig &cfg) noexcept {
    if (render.mode == render::data::RenderMode::World) {
      const auto &first_tm = render.active_chunks[0].tilemap;
      const float half_tw = static_cast<float>(first_tm.diamond_w()) * cfg.tile_scale * 0.5f;
      const float half_th = static_cast<float>(first_tm.diamond_h()) * cfg.tile_scale * 0.5f;
      const auto &manifest = render.manifest;
      const int total_h = manifest.tiles_tall > 0 ? manifest.tiles_tall : manifest.chunks_tall * manifest.chunk_size;
      const float x_origin = static_cast<float>(total_h - 1) * half_tw;
      const auto [iso_w, iso_h] = gameplay::world::tilemap::world_bounds_iso(manifest, half_tw);
      return {.collisions = render.agg_collisions.view(),
              .collision_triangles = render.agg_triangles.view(),
              .world_w_px = iso_w,
              .world_h_px = iso_h,
              .half_tw = half_tw,
              .half_th = half_th,
              .x_origin = x_origin,
              .sprite_scale = static_cast<float>(cfg.sprite_scale),
              .tile_scale = static_cast<float>(cfg.tile_scale),
              .portals = {}};
    }

    const auto &tm = render.map_data.tilemap;
    const float half_tw = static_cast<float>(tm.diamond_w()) * cfg.tile_scale * 0.5f;
    const float half_th = static_cast<float>(tm.diamond_h()) * cfg.tile_scale * 0.5f;
    const float x_origin = static_cast<float>(tm.height - 1) * half_tw;
    const float iso_extent = static_cast<float>(tm.width + tm.height - 1) * half_tw * 2.f;
    return {.collisions = tm.collisions.view(),
            .collision_triangles = tm.collision_triangles.view(),
            .world_w_px = iso_extent,
            .world_h_px = iso_extent,
            .half_tw = half_tw,
            .half_th = half_th,
            .x_origin = x_origin,
            .sprite_scale = static_cast<float>(cfg.sprite_scale),
            .tile_scale = static_cast<float>(cfg.tile_scale),
            .portals = render.map_data.portals};
  }

} // namespace corundum::gameplay::world
