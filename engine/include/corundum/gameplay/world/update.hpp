#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/input/actions.hpp>

#include <span>

namespace corundum::gameplay::world {

  /**
   * @brief Non-owning view over the map data the update loop needs.
   *
   * Carries pre-built, world-space collision data and total world dimensions
   * so the update loop can work for both single-tilemap and multi-chunk modes.
   */
  struct MapView {
    corundum::gameplay::world::tilemap::CollisionRectsView collisions;
    corundum::gameplay::world::tilemap::CollisionTrianglesView collision_triangles;
    float world_w_px = 0.f; ///< Total isometric world width in display pixels.
    float world_h_px = 0.f; ///< Total isometric world height in display pixels.
    float half_tw = 0.f;    ///< Half the scaled diamond width; used for iso↔cart conversion.
    float half_th = 0.f;    ///< Half the scaled diamond height; used for iso↔cart conversion.
    float x_origin = 0.f;   ///< Isometric x-shift so the leftmost tile lands at x = 0.
    std::span<const corundum::gameplay::world::Portal> portals;
  };

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
   */
  void update(corundum::gameplay::world::Scene &scene, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::dialogue::Registry &graphs, const corundum::input::InputState &input,
              const MapView &map, float dt);

} // namespace corundum::gameplay::world
