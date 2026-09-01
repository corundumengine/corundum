#pragma once
#include <corundum/dialogue/action.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/ecs/world.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/portals/transition_prompt.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <optional>
#include <vector>

namespace corundum::world {

  /** @brief Whether the player is free-roaming, locked into a dialogue session, paused on a portal-confirm prompt, or
   * browsing the inventory panel.
   */
  enum class GameMode { Exploring, Dialogue, Prompt, Inventory };

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
  /** @brief Actor entities spawned for one resident world chunk. */
  struct ChunkActorSet {
    corundum::world::tilemap::ChunkCoord coord{};
    std::vector<corundum::ecs::EntityId> entities;
  };

  struct Scene {
    corundum::ecs::EntityId player;
    corundum::ecs::World world;

    Camera camera;
    corundum::dialogue::State dialogue;
    std::optional<corundum::ecs::EntityId> dialogue_npc;
    std::optional<corundum::sprites::AnimId> dialogue_npc_saved_anim;
    std::optional<corundum::ecs::FacingDir> dialogue_npc_saved_facing;
    float elapsed_time = 0.f;
    std::optional<corundum::world::TileCoord> hovered_tile; ///< Updated once per frame by pick_tile().
    GameMode mode = GameMode::Exploring;
    int inventory_cursor =
        0; ///< Highlighted row while mode == GameMode::Inventory; clamped against the row count at render time.
    std::vector<corundum::world::TileCoord> path; ///< Remaining click-to-move waypoints, front = next.
    std::vector<corundum::dialogue::EventAction> pending_dialogue_events;
    std::optional<MapTransition> pending_transition;
    std::optional<TransitionPrompt> transition_prompt;
    std::vector<ChunkActorSet>
        chunk_actors; ///< World mode: per-chunk actor entities, kept in sync with the streaming window.
  };

} // namespace corundum::world
