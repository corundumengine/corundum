#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/scene.hpp>

namespace corundum::quest {
  class Registry;
}

namespace corundum::world {

  /**
   * @brief Advance game state by one fixed timestep.
   *
   * Drives dialogue or exploring physics depending on the current mode.
   * Writes a pending_transition into @p scene when the player steps on a portal.
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
   */
  void update(corundum::world::Scene &scene, const corundum::core::GameConfig &cfg,
              const corundum::dialogue::Registry &graphs, const corundum::input::InputState &input, const MapView &map,
              float dt, float win_w, float win_h, corundum::world::FlagStore &flags,
              const quest::Registry *quests = nullptr);

} // namespace corundum::world
