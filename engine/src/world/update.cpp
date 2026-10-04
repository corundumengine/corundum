// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/portals/transition_prompt.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/ui_stack.hpp>
#include <corundum/world/update.hpp>

#include <corundum/animation/animation_system.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/picking.hpp>

#include <cstdint>

namespace {

  void update_exploring(corundum::world::Scene &scene, const corundum::input::InputState &input,
                        const corundum::world::MapView &map, const corundum::core::GameConfig &cfg, float dt,
                        float win_w, float win_h, corundum::core::math::IsometricParams iso) {
    using corundum::entities::EntityId;

    auto &world = scene.world;
    const EntityId player = scene.player;
    const bool has_player = player_present(scene);

    if (has_player) {
      corundum::physics::update_player(world.transforms, world.collisions, player, input, cfg.player_speed, map, scene,
                                       dt);
    }

    // Integrate all NPCs (player was already integrated inside update_player).
    // NPC velocities are zero today, but when AI gives them motion this establishes
    // a clear integration step separate from the player update.
    for (const EntityId e : world.transforms.active_entities()) {
      if (e != player)
        corundum::physics::integrate(world.transforms, e, dt);
    }

    corundum::animation::update(world.sprites, world.transforms, world.animations, world.facings, world.motion_sprites,
                                iso, cfg.player_speed, dt);

    if (!has_player)
      return;

    const std::uint32_t player_slot = world.transforms.dense_index(player);
    const float player_col = world.transforms.col[player_slot];
    const float player_row = world.transforms.row[player_slot];
    // Elevation term matches the renderer's entity path (render_system.cpp) so the camera tracks
    // the player's actual screen position — omitting it made the camera jitter relative to the
    // sprite while crossing a ramp. Null elevation_map (chunked/streamed World mode) isn't wired
    // up for elevation yet, so it falls back to 0, same as elsewhere in MapView consumers.
    const float elevation = corundum::world::elevation_at_tile(map, player_col, player_row);
    // Camera tracks the player's cell-center anchor (same as the sprite) so the
    // camera and actor stay in lockstep instead of drifting half_th apart.
    const auto [player_pos_x, player_pos_y] =
        corundum::core::math::tile_to_world_center(player_col, player_row, elevation, iso);
    scene.camera.follow_player(player_pos_x, player_pos_y, map, win_w, win_h);
  }

  // Zoom rate for held keyboard/gamepad zoom, in "scroll notches" per second — a feel
  // constant, not a GameConfig field, same rationale as follow_player's margins.
  constexpr float k_zoom_rate_per_sec = 3.f;

  void update_zoom(corundum::world::Scene &scene, const corundum::input::InputState &input,
                   const corundum::core::GameConfig &cfg, float dt, float win_w, float win_h) {
    using corundum::input::Action;

    if (input.scroll_delta_y != 0.f) {
      scene.camera.apply_zoom(input.scroll_delta_y, input.mouse_x, input.mouse_y, cfg.min_zoom, cfg.max_zoom);
    }

    const float button_zoom =
        (input.is_held(Action::ZoomIn) ? 1.f : 0.f) - (input.is_held(Action::ZoomOut) ? 1.f : 0.f);
    if (button_zoom != 0.f) {
      const float center_x = win_w * 0.5f;
      const float center_y = win_h * 0.5f;
      scene.camera.apply_zoom(button_zoom * k_zoom_rate_per_sec * dt, center_x, center_y, cfg.min_zoom, cfg.max_zoom);
    }
  }

  /// Step a paused-on-prompt scene: TransitionPrompt::step decodes the input and returns
  /// Confirmed (commit and resume), Dismissed (Cancel / Select-on-No; resumes with the
  /// prompt latched as declined), or Pending (stay paused). The physics system resets
  /// the prompt once the player walks off the portal rect, so re-entering starts with
  /// Yes highlighted again.
  void update_transition_prompt(corundum::world::Scene &scene, const corundum::input::InputIntent &intent) {
    using Step = corundum::world::TransitionPrompt::Step;

    if (!scene.transition_prompt) {
      // Defensive: a stale Prompt layer with no candidate should not block the player.
      scene.ui.pop();
      return;
    }

    switch (scene.transition_prompt->step(intent)) {
      case Step::Confirmed:
        scene.pending_transition = scene.transition_prompt->transition();
        scene.transition_prompt.reset();
        scene.path.clear(); // don't auto-walk back onto the trigger
        scene.ui.pop();
        break;
      case Step::Dismissed:
        scene.path.clear();
        scene.ui.pop();
        break;
      case Step::Pending:
        break;
    }
  }

} // namespace

namespace corundum::world {

  void update(Scene &scene, const corundum::core::GameConfig &cfg, const corundum::input::InputState &input,
              const MapView &map, float dt, float win_w, float win_h, input::InputDevice last_device) {
    const input::InputIntent intent = input::make_input_intent(input, last_device);

    // Camera zoom is only applied while free-roaming: update_exploring re-clamps the
    // viewport via follow_player on the same step, which apply_zoom requires.
    if (scene.mode() == GameMode::Exploring)
      update_zoom(scene, input, cfg, dt, win_w, win_h);

    // One projection for the whole frame: animation speed scaling (screen-space velocity),
    // camera tracking (cell-center anchor), and tile picking share the same params.
    // elev_step is pre-multiplied by tile_scale to match the renderer's already-scaled
    // iso.elev_step (see compute_isometric_params()).
    const corundum::core::math::IsometricParams iso{
        .half_tw = map.half_tw,
        .half_th = map.half_th,
        .x_origin = map.x_origin,
        .elev_step = cfg.elevation_step_px * map.tile_scale,
    };
    scene.hovered_tile = corundum::world::pick_tile(input.mouse_x, input.mouse_y, scene.camera, map, iso);

    switch (scene.mode()) {
      case corundum::world::GameMode::Prompt:
        update_transition_prompt(scene, intent);
        break;
      case corundum::world::GameMode::Exploring:
        update_exploring(scene, input, map, cfg, dt, win_w, win_h, iso);
        break;
      default:
        // Extension modes and the engine screens (Menu, Settings) run no engine world
        // simulation here: step-owning screens skip world::update entirely, and a
        // non-step-owning extension mode (Dialogue) does its per-step work from a
        // fixed_step_systems entry. In particular Dialogue must never run update_exploring.
        break;
    }
  }

} // namespace corundum::world
