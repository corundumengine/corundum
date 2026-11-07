// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/components.hpp>
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
#include <optional>

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
                                world.clips, iso, cfg.player_speed, dt);

    corundum::animation::update_clips(world.sprites, world.transforms, world.animations, world.facings, world.clips,
                                      dt);
    corundum::entities::mark_finished_clips_for_deletion(world);

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
      case Step::Pending: break;
    }
  }

  /// Resolve this step's interact press into the entity it targets, or nullopt when no
  /// interactable is in range. The engine owns the generic targeting (proximity plus click aim);
  /// gameplay owns whether the target actually starts a dialogue, since it holds the graph
  /// registry. A mouse click must be aimed at the target's tile; a keyboard/gamepad press is
  /// gated on proximity alone.
  [[nodiscard]] std::optional<corundum::entities::EntityId>
  pick_interaction_target(const corundum::world::Scene &scene, const corundum::core::GameConfig &cfg,
                          const corundum::input::InputIntent &intent) {
    using corundum::entities::distance;
    using corundum::entities::EntityId;
    using corundum::entities::Position;

    if (!intent.activate)
      return std::nullopt;

    // A despawned player has no position to measure interaction range from.
    if (!corundum::world::player_present(scene))
      return std::nullopt;

    const bool via_click = intent.cursor_clicked;
    const corundum::entities::World &world = scene.world;
    const std::uint32_t player_slot = world.transforms.dense_index(scene.player);
    const float player_col = world.transforms.col[player_slot];
    const float player_row = world.transforms.row[player_slot];

    std::optional<EntityId> best;
    float best_distance = cfg.interact_radius;
    for (const EntityId eid : world.dialogue_refs.active_entities()) {
      if (!world.transforms.has(eid))
        continue;

      const std::uint32_t npc_slot = world.transforms.dense_index(eid);
      const float npc_col = world.transforms.col[npc_slot];
      const float npc_row = world.transforms.row[npc_slot];
      const float npc_distance =
          distance(Position{.col = player_col, .row = player_row}, Position{.col = npc_col, .row = npc_row});
      if (npc_distance > best_distance)
        continue;

      if (via_click) {
        const corundum::world::TileCoord npc_tile{.col = static_cast<int>(npc_col), .row = static_cast<int>(npc_row)};
        if (!scene.hovered_tile || *scene.hovered_tile != npc_tile)
          continue;
      }

      best = eid;
      best_distance = npc_distance;
    }
    return best;
  }

} // namespace

namespace corundum::world {

  void update(Scene &scene, const corundum::core::GameConfig &cfg, const corundum::input::InputState &input,
              const MapView &map, float dt, float win_w, float win_h, input::InputDevice last_device,
              WorldStep world_step) {
    const input::InputIntent intent = input::make_input_intent(input, last_device);

    // The interact target is a one-frame pulse, so a step that doesn't resolve one reports no
    // target rather than reusing the previous step's.
    scene.pending_interaction.reset();

    // Camera zoom is only applied while the world runs: update_exploring re-clamps the
    // viewport via follow_player on the same step, which apply_zoom requires.
    const bool is_exploring = scene.mode() == GameMode::Exploring;
    if (is_exploring || world_step == WorldStep::Runs)
      update_zoom(scene, input, cfg, dt, win_w, win_h);

    // One projection for the whole frame: animation speed scaling (screen-space velocity),
    // camera tracking (cell-center anchor), and tile picking share the same params.
    // elev_step is pre-multiplied by tile_scale to match the renderer's already-scaled
    // iso.elev_step (see compute_isometric_params()).
    const corundum::core::math::IsometricParams iso = projection_of(map, cfg);
    scene.hovered_tile = corundum::world::pick_tile(input.mouse_x, input.mouse_y, scene.camera, map, iso);

    switch (scene.mode()) {
      case corundum::world::GameMode::Prompt: update_transition_prompt(scene, intent); break;
      case corundum::world::GameMode::Exploring:
        update_exploring(scene, input, map, cfg, dt, win_w, win_h, iso);
        // The engine's exploring input handler records the generic interact target; the gameplay
        // fixed-step system consumes the one-frame pulse (see Scene::pending_interaction).
        scene.pending_interaction = pick_interaction_target(scene, cfg, intent);
        break;
      default:
        // A mode that runs the world (a fight) moves everyone as Exploring does but targets no
        // interaction, so a click on an NPC starts no dialogue. A frozen mode (Dialogue) runs
        // nothing here, and step-owning screens skip world::update entirely.
        if (world_step == WorldStep::Runs)
          update_exploring(scene, input, map, cfg, dt, win_w, win_h, iso);
        break;
    }
  }

} // namespace corundum::world
