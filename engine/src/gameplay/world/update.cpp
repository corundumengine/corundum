#include <corundum/gameplay/world/update.hpp>

#include <corundum/anim/sys/anim_sys.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/sys/camera_system.hpp>
#include <corundum/gameplay/sys/dialogue_system.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/sys/physics_sys.hpp>
#include <corundum/resources/sprite.hpp>

#include <array>

namespace {

  void update_exploring(corundum::gameplay::world::Scene &scene, const corundum::input::InputState &input,
                        const corundum::gameplay::world::MapView &map, const corundum::core::GameConfig &cfg, float dt,
                        float win_w, float win_h) {
    using corundum::gameplay::entity::EntityId;

    auto &world = scene.world;
    const EntityId player = scene.player;

    corundum::physics::sys::update_player(world.transforms, world.collisions, player, input, cfg.player_speed, map,
                                          scene, dt);

    // Integrate all NPCs (player was already integrated inside update_player).
    // NPC velocities are zero today, but when AI gives them motion this establishes
    // a clear integration step separate from the player update.
    for (uint16_t i = 0; i < world.transforms.count; ++i) {
      const EntityId e = world.transforms.idx.entities[i];
      if (e != player)
        corundum::physics::sys::integrate(world.transforms, e, dt);
    }

    // One projection for the whole frame: animation speed scaling (screen-space
    // velocity) and camera tracking (cell-center anchor) share the same params.
    // elev_step here is pre-multiplied by tile_scale to match the renderer's
    // already-scaled iso.elev_step (see compute_isometric_params()), so the
    // camera tracks the entity's true screen position across zoom levels.
    const corundum::core::math::IsometricParams iso{map.half_tw, map.half_th, map.x_origin,
                                                    cfg.elevation_step_px * map.tile_scale};

    corundum::anim::sys::animate(world.sprites, world.transforms, world.animations, world.facings, world.motion_sprites,
                                 iso, cfg.player_speed, dt);

    const auto p_slot = world.transforms.dense_idx(player);
    const float pc = world.transforms.col[p_slot];
    const float pr = world.transforms.row[p_slot];
    // Elevation term matches the renderer's entity path (render_sys.cpp) so the camera tracks
    // the player's actual screen position — omitting it made the camera jitter relative to the
    // sprite while crossing a ramp. Null elevation_map (chunked/streamed World mode) isn't wired
    // up for elevation yet, so it falls back to 0, same as elsewhere in MapView consumers.
    const float elev = corundum::gameplay::world::elevation_at_tile(map, pc, pr);
    // Camera tracks the player's cell-center anchor (same as the sprite) so the
    // camera and actor stay in lockstep instead of drifting half_th apart.
    const auto [pp_x, pp_y] = corundum::core::math::tile_to_world_center(pc, pr, elev, iso);
    corundum::gameplay::sys::follow_player(scene.camera, pp_x, pp_y, map, win_w, win_h);
  }

  // Zoom rate for held keyboard/gamepad zoom, in "scroll notches" per second — a feel
  // constant, not a GameConfig field, same rationale as follow_player's margins.
  constexpr float k_zoom_rate_per_sec = 3.f;

  void update_zoom(corundum::gameplay::world::Scene &scene, const corundum::input::InputState &input,
                   const corundum::core::GameConfig &cfg, float dt, float win_w, float win_h) {
    using corundum::input::Action;

    if (input.scroll_delta_y != 0.f) {
      corundum::gameplay::sys::apply_zoom(scene.camera, input.scroll_delta_y, input.mouse_x, input.mouse_y,
                                          cfg.min_zoom, cfg.max_zoom);
    }

    const float button_zoom =
        (input.is_held(Action::ZoomIn) ? 1.f : 0.f) - (input.is_held(Action::ZoomOut) ? 1.f : 0.f);
    if (button_zoom != 0.f) {
      const float center_x = win_w * 0.5f;
      const float center_y = win_h * 0.5f;
      corundum::gameplay::sys::apply_zoom(scene.camera, button_zoom * k_zoom_rate_per_sec * dt, center_x, center_y,
                                          cfg.min_zoom, cfg.max_zoom);
    }
  }

} // namespace

namespace corundum::gameplay::world {

  void update(corundum::gameplay::world::Scene &scene, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::dialogue::Registry &graphs, const corundum::input::InputState &input,
              const MapView &map, float dt, float win_w, float win_h, corundum::gameplay::FlagStore &flags,
              const quest::Registry *quests) {
    const auto actions = corundum::input::pressed_actions(input);

    update_zoom(scene, input, cfg, dt, win_w, win_h);

    scene.hovered_tile = corundum::gameplay::sys::pick_tile(input.mouse_x, input.mouse_y, scene.camera, map,
                                                            cfg.elevation_step_px * map.tile_scale, scene.camera.zoom);

    if (scene.mode == corundum::gameplay::world::GameMode::Dialogue) [[unlikely]] {
      corundum::gameplay::sys::update_dialogue(scene, actions, flags, quests);
    } else [[likely]] {
      update_exploring(scene, input, map, cfg, dt, win_w, win_h);
      corundum::gameplay::sys::try_interact(scene, input, cfg, graphs, flags);
    }
  }

} // namespace corundum::gameplay::world
