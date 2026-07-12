#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/render/data/render_state.hpp>
#include <corundum/render/sys/render_sys.hpp>

namespace corundum::gameplay::world {

  [[nodiscard]] MapView build_map_view(const render::data::RenderState &render, const core::GameConfig &cfg) noexcept {
    if (render.mode == render::data::RenderMode::World) {
      const auto &first_tm = render.active_chunks[0].tilemap;
      const auto &manifest = render.manifest;
      const int total_h = manifest.tiles_tall > 0 ? manifest.tiles_tall : manifest.chunks_tall * manifest.chunk_size;
      const auto iso =
          core::math::compute_iso_params(first_tm.diamond_w(), first_tm.diamond_h(), total_h, cfg.tile_scale);
      const auto [iso_w, iso_h] = gameplay::world::tilemap::world_bounds_iso(manifest, iso.half_tw, iso.half_th);
      const float total_w = manifest.tiles_wide > 0 ? static_cast<float>(manifest.tiles_wide)
                                                    : static_cast<float>(manifest.chunks_wide * manifest.chunk_size);
      return {.collisions = render.agg_collisions.view(),
              .collision_triangles = render.agg_triangles.view(),
              .world_w_px = iso_w,
              .world_h_px = iso_h,
              .world_w_tiles = total_w,
              .world_h_tiles = static_cast<float>(total_h),
              .half_tw = iso.half_tw,
              .half_th = iso.half_th,
              .x_origin = iso.x_origin,
              .character_scale = cfg.character_scale,
              .tile_scale = cfg.tile_scale,
              .portals = {},
              .world_render = &render};
    }

    const auto &tm = render.map_data.tilemap;
    const auto iso = core::math::compute_iso_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale);
    const float steps = static_cast<float>(tm.width + tm.height - 1);
    return {.collisions = tm.collisions.view(),
            .collision_triangles = tm.collision_triangles.view(),
            .world_w_px = steps * iso.half_tw * 2.f,
            .world_h_px = steps * iso.half_th * 2.f,
            .world_w_tiles = static_cast<float>(tm.width),
            .world_h_tiles = static_cast<float>(tm.height),
            .half_tw = iso.half_tw,
            .half_th = iso.half_th,
            .x_origin = iso.x_origin,
            .character_scale = cfg.character_scale,
            .tile_scale = cfg.tile_scale,
            .portals = render.map_data.portals,
            .elevation_map = &tm,
            .walkability = &render.map_walkability};
  }

  float elevation_at_tile(const MapView &map, float col_f, float row_f) noexcept {
    if (map.world_render)
      return render::sys::elevation_under(*map.world_render, col_f, row_f);
    if (map.elevation_map)
      return corundum::gameplay::world::tilemap::interpolated_elevation_at(*map.elevation_map, col_f, row_f);
    return 0.f;
  }

} // namespace corundum::gameplay::world
