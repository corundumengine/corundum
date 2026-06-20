#pragma once
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/ecs/world.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/world/camera.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/resources/sprite.hpp>

#include <optional>

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
    corundum::gameplay::ecs::World ecs_world;
    corundum::gameplay::ecs::EntityId player;

    GameMode mode = GameMode::Exploring;
    Camera camera;
    corundum::gameplay::dialogue::State dialogue;
    corundum::gameplay::FlagStore flags;
    float elapsed_time = 0.f;
    std::optional<MapTransition> pending_transition;
    std::optional<corundum::gameplay::ecs::EntityId> dialogue_npc;
    std::optional<corundum::gameplay::ecs::FacingDir> dialogue_npc_saved_facing;
    std::optional<corundum::resources::AnimId> dialogue_npc_saved_anim;
  };

} // namespace corundum::gameplay::world
