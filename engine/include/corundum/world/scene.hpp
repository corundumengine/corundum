// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
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

/** Actor entities spawned for one resident world chunk. */
struct ChunkActorSet {
  tilemap::ChunkCoord coord{};

  /** Set when the chunk leaves the active window and its actors are queued for deletion. A set
   *  that is not departed has already spawned (or failed) for the current residency, so it is
   *  never re-read — even once gameplay has despawned every actor it spawned.
   */
  bool departed{};

  std::vector<entities::EntityId> entities{};

  /** Set when the chunk's spawn file was read but spawning failed; the set stays tracked so the
   *  load is not retried every frame while the chunk remains resident.
   */
  bool load_failed{};
};

/** All game-world data for a running session, owned by Engine::scene.
 *
 *  Groups the entity world with the session's navigation, dialogue, transition, and
 *  presentation state. Systems receive Scene& and read or mutate these fields directly.
 *
 *  @note Scene does not own the tilemap. Use Engine::active_tilemap() for map
 *        queries. See the render layer (RenderState::area) for tilemap ownership.
 */
struct Scene {
  entities::World world;

  /** World mode: per-chunk actor entities, kept in sync with the streaming window. */
  std::vector<ChunkActorSet> chunk_actors;

  /** Remaining click-to-move waypoints, front = next. */
  std::vector<world::TileCoord> path;

  /** While true the player doesn't walk: held movement keys and click-to-move are ignored, and
   *  `path` is cleared each step. A fight sets it while the protagonist can't move.
   */
  bool is_player_held{};

  /** Current zone identity: the tilemap path stem for interiors and the world
   *  manifest directory name for overworld mode. `local.<key>` flag references
   *  resolve to `zone.<zone_id>.<key>` against this value.
   */
  std::string zone_id;

  /** Interact target resolved for the current fixed step: the entity the player's Activate
   *  press targeted, or nullopt when it targeted nothing.
   *
   *  @note A one-frame pulse, not a latch. world::update() clears it at the start of every step
   *        and only the exploring branch sets it; the gameplay fixed-step system reads and clears
   *        it, so an unconsumed value (no NPC in range, wrong mode) never reaches the next step.
   */
  std::optional<entities::EntityId> pending_interaction;

  std::optional<MapTransition> pending_transition;

  std::optional<TransitionPrompt> transition_prompt;

  float elapsed_time{0.f};

  /** Stack of open UI screens; top() is GameMode::Exploring while it is empty. Push to open a
   *  screen, pop (Cancel) to peel one layer.
   */
  UIStack ui;

  entities::EntityId player{};

  Camera camera;

  /** Updated once per frame by pick_tile(). */
  std::optional<world::TileCoord> hovered_tile;

  /** The screen currently on top of the UI stack, or GameMode::Exploring when none is open. */
  [[nodiscard]] GameMode mode() const noexcept { return ui.top(); }
};

/** True when scene.player names a live entity with a transform.
 *
 *  The player may legitimately be absent (death, possession, a party swap), so callers that
 *  read the player's transform must check this first.
 */
[[nodiscard]] inline bool player_present(const Scene &scene) noexcept {
  return scene.world.transforms.has(scene.player);
}

} // namespace corundum::world
