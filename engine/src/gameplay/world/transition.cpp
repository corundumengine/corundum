#include <corundum/engine.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/sys/camera_system.hpp>
#include <corundum/gameplay/world/map_view.hpp>
#include <corundum/gameplay/world/spawn.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/render/sys/render_sys.hpp>

#include <algorithm>
#include <print>

namespace corundum::gameplay::world {

  std::expected<void, std::string> enter_world(corundum::Engine &engine, const render::sys::WorldLoadParams &params) {
    auto world_result = render::sys::load_world(*engine.renderer, engine.render, engine.cfg, params);
    if (!world_result)
      return std::unexpected(std::move(world_result).error());
    const auto &info = *world_result;
    const corundum::gameplay::component::Position spawn_pos{info.spawn_world_pos.x, info.spawn_world_pos.y};

    auto scene_result = gameplay::world::spawn_world(engine.cfg, engine.characters,
                                                     engine.render.chunks.active_at(0).tilemap, spawn_pos);
    if (!scene_result)
      return std::unexpected(std::move(scene_result).error());
    engine.scene = std::move(*scene_result);

    const auto [world_width, world_height] =
        corundum::gameplay::world::tilemap::world_bounds_iso(engine.render.manifest, info.half_tw, info.half_th);
    // Convert tile-grid spawn position to isometric for camera tracking. Cell-center anchor
    // keeps the camera aligned with the player. elev_step is pre-multiplied by tile_scale to
    // match compute_isometric_params() for consistency with the renderer's scaled iso.elev_step.
    const corundum::core::math::IsometricParams iso{info.half_tw, info.half_th, info.x_origin,
                                                    engine.cfg.elevation_step_px * engine.cfg.tile_scale};
    const auto [iso_x, iso_y] = corundum::core::math::tile_to_world_center(spawn_pos.col, spawn_pos.row, 0.f, iso);

    engine.scene.camera.zoom = std::clamp(engine.cfg.default_zoom, engine.cfg.min_zoom, engine.cfg.max_zoom);
    // Use the live window size when the loop has populated it; at boot it is still 0
    // and the configured size is the only known viewport.
    const float vw = engine.win_w > 0 ? static_cast<float>(engine.win_w) : static_cast<float>(engine.cfg.win_w);
    const float vh = engine.win_h > 0 ? static_cast<float>(engine.win_h) : static_cast<float>(engine.cfg.win_h);
    corundum::gameplay::sys::center_on(engine.scene.camera, iso_x, iso_y, world_width, world_height,
                                       static_cast<float>(vw), static_cast<float>(vh));
    return {};
  }

  void handle_map_transition(corundum::Engine &engine) noexcept {
    if (!engine.scene.pending_transition)
      return;
    const auto t = *engine.scene.pending_transition;
    engine.scene.pending_transition.reset();

    if (t.return_to_world) {
      // Re-enter the overworld, positioned at the exit portal's spawn tile (the exit portal
      // always carries one). A World→interior journey sets the marker; entering an interior
      // without ever leaving a world leaves it clear.
      const bool can_return = engine.entered_from_world || !engine.cfg.paths.world_manifest_path.empty();
      if (can_return) {
        engine.entered_from_world = false;
        render::sys::WorldLoadParams params;
        params.spawn_col = t.spawn_col;
        params.spawn_row = t.spawn_row;
        if (auto result = enter_world(engine, params); !result) {
          std::println(stderr, "[engine] return-to-world failed: {}", result.error());
          request_quit(engine);
          engine.window->close();
        }
      } else {
        // No overworld was ever loaded (single-map config with an exit portal): there is
        // nowhere to return to, so ignore the portal instead of terminating the game.
        std::println(stderr, "[engine] return_to_world portal traversed with no overworld configured; ignoring");
      }
      return;
    }

    // Cross-map (enter-interior) transition. Mark the journey only when actually leaving
    // the World — an interior→interior (nested) cross-map transition must not overwrite it.
    if (engine.render.mode == render::data::RenderMode::World)
      engine.entered_from_world = true;

    auto map_result = render::sys::load_map(*engine.renderer, engine.render, t.target_map, engine.cfg);
    if (!map_result) {
      std::println(stderr, "[engine] map transition failed: {}", map_result.error());
      request_quit(engine);
      engine.window->close();
      return;
    }

    const auto &new_tm = *active_tilemap(engine);
    const gameplay::component::Position spawn{static_cast<float>(t.spawn_col), static_cast<float>(t.spawn_row)};
    auto scene_result = gameplay::world::spawn_world(engine.cfg, engine.characters, new_tm, spawn);
    if (!scene_result) {
      std::println(stderr, "[engine] map transition failed: {}", scene_result.error());
      request_quit(engine);
      engine.window->close();
      return;
    }
    engine.scene = std::move(*scene_result);
    engine.scene.camera = {};
  }

} // namespace corundum::gameplay::world
