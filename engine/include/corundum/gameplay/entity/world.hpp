#pragma once
#include <array>
#include <cassert>
#include <corundum/gameplay/component/animation_table.hpp>
#include <corundum/gameplay/component/collision_table.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/component/dialogue_table.hpp>
#include <corundum/gameplay/component/facing_table.hpp>
#include <corundum/gameplay/component/motion_sprite_table.hpp>
#include <corundum/gameplay/component/sprite_table.hpp>
#include <corundum/gameplay/component/transform_name_table.hpp>
#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/entity/entity.hpp>

namespace corundum::gameplay::entity {

  // Types owned by corundum::gameplay::component — imported for convenience.
  using corundum::gameplay::component::Animation;
  using corundum::gameplay::component::AnimationTable;
  using corundum::gameplay::component::CollisionTable;
  using corundum::gameplay::component::DialogueRef;
  using corundum::gameplay::component::DialogueTable;
  using corundum::gameplay::component::FacingTable;
  using corundum::gameplay::component::MotionSpriteTable;
  using corundum::gameplay::component::Position;
  using corundum::gameplay::component::Sprite;
  using corundum::gameplay::component::SpriteTable;
  using corundum::gameplay::component::TransformNameTable;
  using corundum::gameplay::component::TransformTable;
  using corundum::gameplay::component::Velocity;

  /// Top-level entity container; owns the entity pool and all component tables.
  /// @note Not thread-safe.
  struct World {
    EntityManager entities;
    TransformTable transforms;          ///< Hot: x, y, dx, dy — updated every frame.
    TransformNameTable transform_names; ///< Cold: debug labels — never read in update loops.
    SpriteTable sprites;
    AnimationTable animations;
    CollisionTable collisions;
    DialogueTable dialogue_refs;
    FacingTable facings;
    MotionSpriteTable motion_sprites;

    /// Buffer for deferred deletion — append via mark_for_deletion(), drain via flush_deletions().
    /// Fixed-size: bounded by k_max_entities, so no heap growth mid-frame.
    std::array<EntityId, k_max_entities> pending_deletions{};
    std::uint32_t pending_deletion_count = 0;
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
  [[nodiscard]] inline EntityId spawn(World &w, Position pos, Velocity vel, Sprite spr, DialogueRef ref) {
    const EntityId e = spawn(w, pos, vel, spr);
    w.dialogue_refs.insert(e, ref.graph_id);
    return e;
  }

  /// Remove e and all of its components from the world immediately.
  /// Safe to call between update frames. During an update, prefer mark_for_deletion().
  /// @pre e must be a live entity.
  inline void despawn(World &w, EntityId e) {
    if (w.transforms.has(e))
      w.transforms.remove(e);
    if (w.transform_names.has(e))
      w.transform_names.remove(e);
    if (w.sprites.has(e))
      w.sprites.remove(e);
    if (w.animations.has(e))
      w.animations.remove(e);
    if (w.collisions.has(e))
      w.collisions.remove(e);
    if (w.dialogue_refs.has(e))
      w.dialogue_refs.remove(e);
    if (w.facings.has(e))
      w.facings.remove(e);
    if (w.motion_sprites.has(e))
      w.motion_sprites.remove(e);
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
    assert(w.pending_deletion_count < k_max_entities && "pending_deletions full");
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

} // namespace corundum::gameplay::entity
