// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/scene.hpp>

namespace corundum::world {

  /**
   * @brief Advance game state by one fixed timestep.
   *
   * Dispatches on the current engine-owned mode: steps the portal-confirm prompt
   * or drives exploring physics. Extension modes and the engine screens fall to a
   * no-op default — step-owning screens skip world::update entirely, and a
   * non-step-owning extension mode (Dialogue) does its per-step work from a
   * fixed_step_systems entry. Writes a pending_transition into @p scene when the
   * player steps on a portal, and a pending_interaction when an interact press
   * resolves to a nearby entity.
   *
   * @param scene       All mutable game-world state.
   * @param cfg         Immutable game configuration.
   * @param input       Current frame input state.
   * @param map         Non-owning view of the current map's tilemap and portals.
   * @param dt          Fixed timestep in seconds.
   * @param win_w       Live window width in screen pixels.
   * @param win_h       Live window height in screen pixels.
   * @param last_device Device of the player's most recent press, for the input-glyph prompts.
   *
   * @note Interaction (starting a dialogue) is gameplay: this function records the generic
   *       interact target in Scene::pending_interaction, and the gameplay fixed-step system
   *       consumes it, so the engine runtime never names a gameplay registry.
   */
  void update(Scene &scene, const corundum::core::GameConfig &cfg, const corundum::input::InputState &input,
              const MapView &map, float dt, float win_w, float win_h,
              input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::world
