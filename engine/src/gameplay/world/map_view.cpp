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
      const auto [iso_w, iso_h] = gameplay::world::tilemap::world_bounds_iso(manifest, iso.half_tw);
      return {.collisions = render.agg_collisions.view(),
              .collision_triangles = render.agg_triangles.view(),
              .world_w_px = iso_w,
              .world_h_px = iso_h,
              .half_tw = iso.half_tw,
              .half_th = iso.half_th,
              .x_origin = iso.x_origin,
              .sprite_scale = static_cast<float>(cfg.sprite_scale),
              .tile_scale = static_cast<float>(cfg.tile_scale),
              .portals = {}};
    }

    const auto &tm = render.map_data.tilemap;
    const auto iso = core::math::compute_iso_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale);
    const float iso_extent = static_cast<float>(tm.width + tm.height - 1) * iso.half_tw * 2.f;
    return {.collisions = tm.collisions.view(),
            .collision_triangles = tm.collision_triangles.view(),
            .world_w_px = iso_extent,
            .world_h_px = iso_extent,
            .half_tw = iso.half_tw,
            .half_th = iso.half_th,
            .x_origin = iso.x_origin,
            .sprite_scale = static_cast<float>(cfg.sprite_scale),
            .tile_scale = static_cast<float>(cfg.tile_scale),
            .portals = render.map_data.portals};
  }

} // namespace corundum::gameplay::world
