// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/item/item.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/portals/transition_prompt.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/update.hpp>

#include <corundum/animation/animation_system.hpp>
#include <corundum/dialogue/interact.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/picking.hpp>

#include <algorithm>
#include <cstdint>

namespace {

  void update_exploring(corundum::world::Scene &scene, const corundum::input::InputState &input,
                        const corundum::world::MapView &map, const corundum::core::GameConfig &cfg, float dt,
                        float win_w, float win_h, corundum::core::math::IsometricParams iso) {
    using corundum::entities::EntityId;

    auto &world = scene.world;
    const EntityId player = scene.player;

    corundum::physics::update_player(world.transforms, world.collisions, player, input, cfg.player_speed, map, scene,
                                     dt);

    // Integrate all NPCs (player was already integrated inside update_player).
    // NPC velocities are zero today, but when AI gives them motion this establishes
    // a clear integration step separate from the player update.
    for (const EntityId e : world.transforms.active_entities()) {
      if (e != player)
        corundum::physics::integrate(world.transforms, e, dt);
    }

    corundum::animation::update(world.sprites, world.transforms, world.animations, world.facings, world.motion_sprites,
                                iso, cfg.player_speed, dt);

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
  void update_transition_prompt(corundum::world::Scene &scene, const corundum::input::InputState &input) {
    using corundum::world::GameMode;
    using Step = corundum::world::TransitionPrompt::Step;

    if (!scene.transition_prompt) {
      // Defensive: a stale Prompt mode with no candidate should not block the player.
      scene.mode = GameMode::Exploring;
      return;
    }

    switch (scene.transition_prompt->step(input)) {
      case Step::Confirmed:
        scene.pending_transition = scene.transition_prompt->transition();
        scene.transition_prompt.reset();
        scene.path.clear(); // don't auto-walk back onto the trigger
        scene.mode = GameMode::Exploring;
        break;
      case Step::Dismissed:
        scene.path.clear();
        scene.mode = GameMode::Exploring;
        break;
      case Step::Pending:
        break;
    }
  }

  /// Modulo wrap of a list cursor, matching the dialogue choice-list cursor
  /// (dialogue/conversation.cpp): Down past the last row lands on the first, Up past
  /// the first lands on the last. `count` must be > 0.
  int wrap_cursor(int current, int delta, int count) noexcept {
    return (current + delta + count) % count;
  }

  /// Step a paused-on-inventory scene: Cancel returns to Exploring (pressing I
  /// again is handled by the toggle in update()); MoveUp/MoveDown wrap the highlight
  /// within the held-item rows (the same count build_inventory_lines renders). Not
  /// calling update_exploring here is what pauses the player (same mechanism as
  /// Dialogue / Prompt). The held-item scan runs only when a move is pressed.
  void update_inventory(corundum::world::Scene &scene, const corundum::input::InputState &input,
                        const corundum::world::FlagStore &flags) {
    using corundum::input::Action;

    if (input.is_pressed(Action::Cancel)) {
      scene.mode = corundum::world::GameMode::Exploring;
      return;
    }

    const bool move_down = input.is_pressed(Action::MoveDown);
    const bool move_up = input.is_pressed(Action::MoveUp);
    if (!move_down && !move_up)
      return;

    const int rows = static_cast<int>(
        std::ranges::count_if(flags, [](const auto &kv) { return corundum::item::is_held_item(kv.first, kv.second); }));
    if (rows <= 0) {
      scene.inventory_cursor = 0;
      return;
    }

    if (move_down)
      scene.inventory_cursor = wrap_cursor(scene.inventory_cursor, +1, rows);
    else
      scene.inventory_cursor = wrap_cursor(scene.inventory_cursor, -1, rows);
  }

} // namespace

namespace corundum::world {

  void update(Scene &scene, const corundum::core::GameConfig &cfg, const corundum::dialogue::Registry &graphs,
              const corundum::input::InputState &input, const MapView &map, float dt, float win_w, float win_h,
              FlagStore &flags, const quest::Registry *quests) {
    if (input.is_pressed(input::Action::Inventory)) {
      if (scene.mode == GameMode::Exploring) {
        scene.mode = GameMode::Inventory;
        scene.inventory_cursor = 0;
      } else if (scene.mode == GameMode::Inventory) {
        scene.mode = GameMode::Exploring;
      }
    }

    // Camera zoom is only applied while free-roaming: update_exploring re-clamps the
    // viewport via follow_player on the same step, which apply_zoom requires.
    if (scene.mode == GameMode::Exploring)
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

    switch (scene.mode) {
      case corundum::world::GameMode::Dialogue: {
        const corundum::input::PressedActions actions = corundum::input::pressed_actions(input);
        corundum::dialogue::update_dialogue(scene, actions);
        break;
      }
      case corundum::world::GameMode::Prompt:
        update_transition_prompt(scene, input);
        break;
      case corundum::world::GameMode::Inventory:
        update_inventory(scene, input, flags);
        break;
      case corundum::world::GameMode::Exploring:
        update_exploring(scene, input, map, cfg, dt, win_w, win_h, iso);
        // update_exploring may have armed a portal prompt (mode → Prompt), whose
        // try_interact @pre requires Exploring.
        if (scene.mode == GameMode::Exploring)
          corundum::dialogue::try_interact(scene, input, cfg, graphs, flags, quests);
        break;
    }
  }

} // namespace corundum::world
