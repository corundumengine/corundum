// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/direction.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/camera.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/portals/transition_prompt.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/ui_stack.hpp>

#include <optional>
#include <string>
#include <vector>

namespace corundum::world {

  /** @brief All game-world data for a running session.
   *
   * Groups the entity world with the session's navigation, dialogue, transition, and
   * presentation state. Systems receive Scene& and read or mutate these fields directly.
   *
   * @note Scene does not own the tilemap. Use Engine::active_tilemap() for map
   *       queries. See the render layer (RenderState::map_data, RenderState::chunks)
   *       for tilemap ownership.
   *
   * @see Engine  For the owning engine struct.
   */
  /** @brief Actor entities spawned for one resident world chunk. */
  struct ChunkActorSet {
    corundum::world::tilemap::ChunkCoord coord{};

    /// Set when the chunk leaves the active window and its actors are queued for deletion. A set
    /// that is not departed has already spawned (or failed) for the current residency, so it is
    /// never re-read — even once gameplay has despawned every actor it spawned.
    bool departed{};

    std::vector<corundum::entities::EntityId> entities{};

    /// Set when the chunk's spawn file was read but spawning failed; the set stays tracked so the
    /// load is not retried every frame while the chunk remains resident.
    bool load_failed{};
  };

  /** @brief The NPC bound to the active dialogue, with the facing and animation saved when the conversation
   * began so they can be restored when it ends.
   *
   * The saved values are set only when the NPC had the corresponding component at bind time.
   */
  struct DialogueNpc {
    corundum::entities::EntityId entity{};

    std::optional<corundum::sprites::AnimId> saved_anim{};

    std::optional<corundum::core::Direction> saved_facing{};
  };

  struct Scene {
    corundum::entities::World world;

    std::vector<ChunkActorSet>
        chunk_actors; ///< World mode: per-chunk actor entities, kept in sync with the streaming window.

    std::vector<corundum::world::TileCoord> path; ///< Remaining click-to-move waypoints, front = next.

    /// Current zone identity: the tilemap path stem for interiors and the world
    /// manifest directory name for overworld mode. `local.<key>` flag references
    /// resolve to `zone.<zone_id>.<key>` against this value.
    std::string zone_id;

    /// Active dialogue conversation; disengaged while not in a dialogue. Owned here so
    /// the presentation layer can query it read-only; stepped by gameplay::dialogue::update_dialogue.
    std::optional<corundum::gameplay::dialogue::Conversation> dialogue;

    std::optional<DialogueNpc> dialogue_npc;

    std::vector<corundum::gameplay::dialogue::EventAction> pending_dialogue_events;

    std::optional<MapTransition> pending_transition;

    std::optional<TransitionPrompt> transition_prompt;

    float elapsed_time{0.f};

    /// Highlighted row while mode == GameMode::Inventory; wrapped against the held-item count by
    /// update_inventory().
    int inventory_cursor{};

    /// Held-item rows of the Inventory hub tab, rebuilt when the tab is opened or switched to.
    /// The inventory is read-only and the simulation is paused while it is open, so there is no
    /// per-frame rebuild (see AGENTS.md, "Cache or hoist per-frame-invariant computation").
    std::vector<corundum::gameplay::screens::InventoryLine> inventory_lines;

    /// Highlighted row while mode == GameMode::Journal; wrapped against the started-quest count by
    /// update_journal().
    int journal_cursor{};

    /// Hub tab the Hub button (gamepad Y) reopens; the last tab that was opened or switched to.
    GameMode last_hub_mode{GameMode::Inventory};

    /// Stack of open UI screens; top() is GameMode::Exploring while it is empty. Push to open a
    /// screen, pop (Cancel) to peel one layer.
    UIStack ui;

    corundum::entities::EntityId player{};

    Camera camera;

    std::optional<corundum::world::TileCoord> hovered_tile; ///< Updated once per frame by pick_tile().

    /// The screen currently on top of the UI stack, or GameMode::Exploring when none is open.
    [[nodiscard]] GameMode mode() const noexcept {
      return ui.top();
    }
  };

  /** @brief True when scene.player names a live entity with a transform.
   *
   *  The player may legitimately be absent (death, possession, a party swap), so callers that
   *  read the player's transform must check this first. */
  [[nodiscard]] inline bool player_present(const Scene &scene) noexcept {
    return scene.world.transforms.has(scene.player);
  }

} // namespace corundum::world
