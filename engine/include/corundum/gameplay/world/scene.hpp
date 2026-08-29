#pragma once
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/portals/transition_prompt.hpp>
#include <corundum/resources/sprite.hpp>

#include <optional>
#include <vector>

namespace corundum::gameplay::world {

  /** @brief Whether the player is free-roaming, locked into a dialogue session, or paused on a portal-confirm prompt.
   */
  enum class GameMode { Exploring, Dialogue, Prompt };

  /** @brief All game-world data for a running session.
   *
   * Merges what was previously WorldState and GameState into a single
   * container. Systems receive Scene& and operate on its tables via std::span views.
   * The Scene has no update logic — it is pure data.
   *
   * @note Scene does not own the tilemap. Use active_tilemap(engine) for map
   *       queries. See the render layer (RenderState::map_data, RenderState::chunks)
   *       for tilemap ownership.
   *
   * @see Engine  For the owning engine struct.
   */
  struct Scene {
    corundum::gameplay::entity::EntityId player;
    corundum::gameplay::entity::World world;

    Camera camera;
    corundum::gameplay::dialogue::State dialogue;
    std::optional<corundum::gameplay::entity::EntityId> dialogue_npc;
    std::optional<corundum::resources::AnimId> dialogue_npc_saved_anim;
    std::optional<corundum::gameplay::component::FacingDir> dialogue_npc_saved_facing;
    float elapsed_time = 0.f;
    std::optional<corundum::gameplay::sys::TileCoord> hovered_tile; ///< Updated once per frame by pick_tile().
    GameMode mode = GameMode::Exploring;
    std::vector<corundum::gameplay::sys::TileCoord> path; ///< Remaining click-to-move waypoints, front = next.
    std::vector<corundum::gameplay::dialogue::EventAction> pending_dialogue_events;
    std::optional<MapTransition> pending_transition;
    std::optional<TransitionPrompt> transition_prompt;
  };

} // namespace corundum::gameplay::world
