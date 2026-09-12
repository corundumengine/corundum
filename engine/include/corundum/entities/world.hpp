// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/actor_id_table.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/entities/tables/dialogue_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_name_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <optional>
#include <string_view>
#include <tuple>

namespace corundum::entities {

  /// Top-level entity container; owns the entity pool and all component tables.
  /// @note Not thread-safe.
  struct World {
    CollisionTable collisions;
    TransformTable transforms; ///< Hot: col, row, dc, dr — updated every frame.
    AnimationTable animations;
    TransformNameTable transform_names; ///< Cold: debug labels — never read in update loops.
    DialogueTable dialogue_refs;
    ActorIdTable actor_ids; ///< Cold: stable authoring ids for quest/save references.

    /// Buffer for deferred deletion — append via mark_for_deletion(), drain via flush_deletions().
    /// Fixed-size: bounded by k_max_entities, so no heap growth mid-frame.
    std::array<EntityId, k_max_entities> pending_deletions{};
    std::uint32_t pending_deletion_count = 0;

    EntityManager entities;
    FacingTable facings;
    SpriteTable sprites;
    MotionSpriteTable motion_sprites;
  };

  /// Spawn a basic entity with position, velocity, and sprite (non-animated NPC).
  [[nodiscard]] inline EntityId spawn(World &w, Position pos, Velocity vel, Sprite spr) {
    const EntityId e = w.entities.create();
    w.transforms.insert(e, pos.col, pos.row, vel.dc, vel.dr);
    w.transform_names.insert(e);
    w.sprites.insert(e, spr.sprite_id, spr.anim_id, spr.frame_index);
    return e;
  }

  /// Spawn a fully animated entity (player).
  [[nodiscard]] inline EntityId spawn(World &w, Position pos, Velocity vel, Sprite spr, Animation anim) {
    const EntityId e = spawn(w, pos, vel, spr);
    w.animations.insert(e);
    w.animations.set_frame_counts(e, anim.frame_counts);
    return e;
  }

  /// Spawn an NPC that triggers a dialogue graph.
  [[nodiscard]] inline EntityId spawn(World &w, Position pos, Velocity vel, Sprite spr, const DialogueRef &ref) {
    const EntityId e = spawn(w, pos, vel, spr);
    w.dialogue_refs.insert(e, ref.graph_id);
    return e;
  }

  /// @brief Tie all component tables into a tuple for fold-expression iteration.
  /// Adding a new table means adding one member to World and one entry in this tie;
  /// despawn marks/deletion and any future cross-table operations update automatically.
  [[nodiscard]] static auto all_tables(World &w) noexcept {
    return std::tie(w.transforms, w.transform_names, w.sprites, w.animations, w.collisions, w.dialogue_refs,
                    w.actor_ids, w.facings, w.motion_sprites);
  }

  /// Remove e and all of its components from the world immediately.
  /// Safe to call between update frames. During an update, prefer mark_for_deletion().
  /// @pre e must be a live entity.
  inline void despawn(World &w, EntityId e) {
    assert(w.entities.is_live(e) && "despawn: not a live entity");
    std::apply([e](auto &...tables) { ((tables.has(e) ? (tables.remove(e), 0) : 0), ...); }, all_tables(w));
    w.entities.destroy(e);
  }

  /** @brief Queue @p e for removal at the next flush_deletions() call.
   *
   * Safe to call mid-iteration — the entity remains live until flush_deletions().
   * @param[in,out] w World that owns @p e.
   * @param[in]     e A live entity not already queued for deletion.
   * @pre @p e must be live (returned by EntityManager::create() and not destroyed).
   */
  inline void mark_for_deletion(World &w, EntityId e) {
    assert(w.entities.is_live(e) && "mark_for_deletion: not a live entity");
    assert(w.pending_deletion_count < k_max_entities && "pending_deletions full");
    for (std::uint32_t i = 0; i < w.pending_deletion_count; ++i)
      if (w.pending_deletions[i] == e)
        return;
    w.pending_deletions[w.pending_deletion_count++] = e;
  }

  /** @brief Despawn all entities queued via mark_for_deletion() and clear the queue.
   *
   * Call once per frame, after all system updates are complete, to safely apply
   * mid-frame deletion requests without invalidating active iterators.
   * @param[in,out] w World whose pending_deletions to drain.
   */
  inline void flush_deletions(World &w) {
    for (std::uint32_t i = 0; i < w.pending_deletion_count; ++i)
      despawn(w, w.pending_deletions[i]);
    w.pending_deletion_count = 0;
  }

  /** @brief Find the live entity carrying a stable authoring id.
   *
   * Linear scan over the dense actor-id rows — id lookups happen only on
   * quest/save resolution, never in a per-frame hot loop.
   * @param[in] w  World to search.
   * @param[in] id Stable authoring id (as authored in a spawn-points file).
   * @return The entity handle, or std::nullopt when no live entity has @p id.
   */
  [[nodiscard]] inline std::optional<EntityId> find_actor(const World &w, std::string_view id) noexcept {
    for (const EntityId e : w.actor_ids.active_entities())
      if (w.actor_ids.get_actor_id(e) == id)
        return e;
    return std::nullopt;
  }

} // namespace corundum::entities
