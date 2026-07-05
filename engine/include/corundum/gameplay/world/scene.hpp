#pragma once
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/sys/picking.hpp>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/resources/sprite.hpp>

#include <optional>
#include <vector>

namespace corundum::gameplay::world {

  /** @brief Whether the player is free-roaming or locked into a dialogue session. */
  enum class GameMode { Exploring, Dialogue };

  /** @brief All game-world data for a running session.
   *
   * Merges what was previously WorldState and GameState into a single
   * container. Systems receive Scene& and operate on its tables via std::span views.
   * The Scene has no update logic — it is pure data.
   *
   * @see Engine  For the owning engine struct.
   */
  struct Scene {
    corundum::gameplay::entity::World world;
    corundum::gameplay::entity::EntityId player;

    GameMode mode = GameMode::Exploring;
    Camera camera;
    corundum::gameplay::dialogue::State dialogue;
    corundum::gameplay::FlagStore flags;
    float elapsed_time = 0.f;
    std::optional<MapTransition> pending_transition;
    std::optional<corundum::gameplay::entity::EntityId> dialogue_npc;
    std::optional<corundum::gameplay::component::FacingDir> dialogue_npc_saved_facing;
    std::optional<corundum::resources::AnimId> dialogue_npc_saved_anim;
    std::vector<corundum::gameplay::dialogue::EventAction> pending_dialogue_events;
    std::optional<corundum::gameplay::sys::TileCoord> hovered_tile; ///< Updated once per frame by pick_tile().
    std::vector<corundum::gameplay::sys::TileCoord> path; ///< Remaining click-to-move waypoints, front = next.
  };

} // namespace corundum::gameplay::world
