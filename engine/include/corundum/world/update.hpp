// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/scene.hpp>

namespace corundum::quest {
  class Registry;
}

namespace corundum::world {

  /**
   * @brief Advance game state by one fixed timestep.
   *
   * Dispatches on the current mode: steps the active dialogue, the portal-confirm prompt,
   * or the inventory, or drives exploring physics. Writes a pending_transition into
   * @p scene when the player steps on a portal.
   *
   * @param scene       All mutable game-world state.
   * @param cfg         Immutable game configuration.
   * @param graphs      Loaded dialogue graphs.
   * @param input       Current frame input state.
   * @param map         Non-owning view of the current map's tilemap and portals.
   * @param dt          Fixed timestep in seconds.
   * @param win_w       Live window width in screen pixels.
   * @param win_h       Live window height in screen pixels.
   * @param flags       Persistent game flags (quest progress, dialogue visit counts).
   * @param quests      Quest registry for dialogue condition evaluation; may be nullptr.
   */
  void update(Scene &scene, const corundum::core::GameConfig &cfg, const corundum::dialogue::Registry &graphs,
              const corundum::input::InputState &input, const MapView &map, float dt, float win_w, float win_h,
              FlagStore &flags, const quest::Registry *quests = nullptr);

} // namespace corundum::world
