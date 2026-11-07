// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <span>

namespace corundum::entities {

  /** @brief SoA table for entities with motion-driven sprite sheet switching.
   *
   * Stores a walk sprite and an idle sprite (plus pre-cached per-direction frame
   * counts for each) for entities whose visible animation depends on whether they
   * are moving. Configurable per-direction transition delays prevent flickering
   * on brief inputs or mid-step stops.
   *
   * Transition logic (handled by animation::update):
   *   - When desired sprite differs from current, a pending transition starts.
   *   - The transition commits only when the pending timer >= the configured delay.
   *   - If the desired sprite reverts to current before the timer fires, the
   *     pending transition is cancelled and the timer resets.
   *
   * An entry with a ready sheet can take a stance: from enter_stance until leave_stance, the ready
   * sheet stands in for the idle sheet everywhere, so animation::update needs no special case.
   *
   * @note Only entities with both a walk sheet and an idle sheet need an entry.
   */
  struct MotionSpriteTable {
    static constexpr auto k_max = k_max_entities;

    /** @brief Spawn-time configuration for one motion-sprite entry. */
    struct Config {
      corundum::sprites::SpriteId walk_id = corundum::sprites::k_null_sprite_id;

      corundum::sprites::SpriteId idle_id = corundum::sprites::k_null_sprite_id;

      std::array<uint8_t, corundum::sprites::k_num_anim_ids> walk_counts{};

      std::array<uint8_t, corundum::sprites::k_num_anim_ids> idle_counts{};

      float idle_to_walk_delay = 0.f;

      float walk_to_idle_delay = 0.f;

      float walk_frame_duration = 0.f;

      float idle_frame_duration = 0.f;

      /// The sheet to stand on in a stance; k_null_sprite_id = the character has none.
      corundum::sprites::SpriteId ready_id = corundum::sprites::k_null_sprite_id;

      std::array<uint8_t, corundum::sprites::k_num_anim_ids> ready_counts{};

      float ready_frame_duration = 0.f;
    };

    SparseIndex<k_max> index;

    std::array<corundum::sprites::SpriteId, k_max> walk_id{};

    std::array<corundum::sprites::SpriteId, k_max> idle_id{};

    std::array<std::array<uint8_t, corundum::sprites::k_num_anim_ids>, k_max> walk_counts{};

    std::array<std::array<uint8_t, corundum::sprites::k_num_anim_ids>, k_max> idle_counts{};

    std::array<float, k_max> idle_to_walk_delay{}; ///< Delay before idle→walk commits.

    std::array<float, k_max> walk_to_idle_delay{}; ///< Delay before walk→idle commits.

    std::array<float, k_max> walk_frame_duration{}; ///< Seconds per frame while walking; 0 = no override.

    std::array<float, k_max> idle_frame_duration{}; ///< Seconds per frame while idle; 0 = no override.

    std::array<corundum::sprites::SpriteId, k_max> ready_id{}; ///< k_null_sprite_id = no ready sheet.

    std::array<std::array<uint8_t, corundum::sprites::k_num_anim_ids>, k_max> ready_counts{};

    std::array<float, k_max> ready_frame_duration{}; ///< Seconds per frame in a stance; 0 = no override.

    std::array<bool, k_max> in_stance{}; ///< Between enter_stance and leave_stance.

    /// SpriteId the entity is transitioning toward; k_null_sprite_id = no pending transition.
    std::array<corundum::sprites::SpriteId, k_max> pending_sid{};

    std::array<float, k_max> transition_timer{}; ///< Accumulated time toward the pending transition.

    std::uint32_t count = 0;

    /** @brief True if @p e has a motion-sprite entry. */
    [[nodiscard]] bool has(EntityId e) const noexcept { return index.has(e); }

    /** @brief Walk SpriteId for @p e. @pre has(e). */
    [[nodiscard]] corundum::sprites::SpriteId walk_sprite(EntityId e) const noexcept {
      assert(has(e));
      return walk_id[index.dense_index(e)];
    }

    /** @brief The SpriteId @p e stands on: its ready sheet in a stance, else its idle sheet. @pre has(e). */
    [[nodiscard]] corundum::sprites::SpriteId idle_sprite(EntityId e) const noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      return in_stance[slot] ? ready_id[slot] : idle_id[slot];
    }

    /** @brief Configured delay (seconds) for the transition to @p target_sid. @pre has(e). */
    [[nodiscard]] float delay_for(EntityId e, corundum::sprites::SpriteId target_sid) const noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      return (target_sid == walk_id[slot]) ? idle_to_walk_delay[slot] : walk_to_idle_delay[slot];
    }

    /** @brief Frame duration override (seconds/frame) for @p target_sid; 0 means use AnimationTable default. @pre
     * has(e). */
    [[nodiscard]] float frame_duration_for(EntityId e, corundum::sprites::SpriteId target_sid) const noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      if (target_sid == walk_id[slot])
        return walk_frame_duration[slot];
      return in_stance[slot] ? ready_frame_duration[slot] : idle_frame_duration[slot];
    }

    /** @brief SpriteId currently pending transition, or k_null_sprite_id if none. @pre has(e). */
    [[nodiscard]] corundum::sprites::SpriteId pending_sprite(EntityId e) const noexcept {
      assert(has(e));
      return pending_sid[index.dense_index(e)];
    }

    /** @brief Walk frame counts for @p e, suitable for AnimationTable::set_frame_counts. @pre has(e). */
    [[nodiscard]] std::array<uint8_t, corundum::sprites::k_num_anim_ids> walk_frame_counts(EntityId e) const noexcept {
      assert(has(e));
      return walk_counts[index.dense_index(e)];
    }

    /** @brief Frame counts of the sheet @p e stands on, suitable for AnimationTable::set_frame_counts.
     *  @pre has(e). */
    [[nodiscard]] std::array<uint8_t, corundum::sprites::k_num_anim_ids> idle_frame_counts(EntityId e) const noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      return in_stance[slot] ? ready_counts[slot] : idle_counts[slot];
    }

    /** @brief True between enter_stance and leave_stance, for a character with a ready sheet. @pre has(e). */
    [[nodiscard]] bool is_in_stance(EntityId e) const noexcept {
      assert(has(e));
      return in_stance[index.dense_index(e)];
    }

    /** @brief Stand on the ready sheet from now on; does nothing with no ready sheet. @pre has(e). */
    void enter_stance(EntityId e) noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      in_stance[slot] = ready_id[slot] != corundum::sprites::k_null_sprite_id;
    }

    /** @brief Stand on the idle sheet again. @pre has(e). */
    void leave_stance(EntityId e) noexcept {
      assert(has(e));
      in_stance[index.dense_index(e)] = false;
    }

    /** @brief Begin tracking a transition to @p target, resetting the timer.
     *  No-op if @p target is already the pending target.
     *  @pre has(e).
     */
    void set_pending(EntityId e, corundum::sprites::SpriteId target) noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      if (pending_sid[slot] == target)
        return;
      pending_sid[slot] = target;
      transition_timer[slot] = 0.f;
    }

    /** @brief Advance the transition timer by @p dt seconds and return the new total.
     *  @pre has(e).
     */
    float tick_transition(EntityId e, float dt) noexcept {
      assert(has(e));
      float &timer = transition_timer[index.dense_index(e)];
      timer += dt;
      return timer;
    }

    /** @brief Cancel any pending transition, resetting the timer to zero. @pre has(e). */
    void cancel_transition(EntityId e) noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      pending_sid[slot] = corundum::sprites::k_null_sprite_id;
      transition_timer[slot] = 0.f;
    }

    /** @brief Add a motion-sprite entry for @p e.
     *  @param[in] e      Entity to add the entry for.
     *  @param[in] config Spawn-time walk/idle sprite ids, frame counts, and transition timings.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, const Config &config) noexcept {
      index.insert(e, count, [this, &config](auto slot) {
        walk_id[slot] = config.walk_id;
        idle_id[slot] = config.idle_id;
        idle_to_walk_delay[slot] = config.idle_to_walk_delay;
        walk_to_idle_delay[slot] = config.walk_to_idle_delay;
        walk_frame_duration[slot] = config.walk_frame_duration;
        idle_frame_duration[slot] = config.idle_frame_duration;
        ready_id[slot] = config.ready_id;
        ready_counts[slot] = config.ready_counts;
        ready_frame_duration[slot] = config.ready_frame_duration;
        in_stance[slot] = false;
        pending_sid[slot] = corundum::sprites::k_null_sprite_id;
        transition_timer[slot] = 0.f;
        walk_counts[slot] = config.walk_counts;
        idle_counts[slot] = config.idle_counts;
      });
    }

    /** @brief Remove @p e's entry via swap-and-pop. @pre has(e). */
    void remove(EntityId e) noexcept {
      index.remove(e, count, [this](auto slot, auto last) {
        walk_id[slot] = walk_id[last];
        idle_id[slot] = idle_id[last];
        idle_to_walk_delay[slot] = idle_to_walk_delay[last];
        walk_to_idle_delay[slot] = walk_to_idle_delay[last];
        walk_frame_duration[slot] = walk_frame_duration[last];
        idle_frame_duration[slot] = idle_frame_duration[last];
        ready_id[slot] = ready_id[last];
        ready_counts[slot] = ready_counts[last];
        ready_frame_duration[slot] = ready_frame_duration[last];
        in_stance[slot] = in_stance[last];
        pending_sid[slot] = pending_sid[last];
        transition_timer[slot] = transition_timer[last];
        walk_counts[slot] = walk_counts[last];
        idle_counts[slot] = idle_counts[last];
      });
    }

    [[nodiscard]] auto active_span(this auto &self) noexcept { return std::span(self.walk_id).first(self.count); }

    [[nodiscard]] auto active_entities(this auto &self) noexcept { return self.index.active_entities(self.count); }
  };

  static_assert(GameTable<MotionSpriteTable>);

} // namespace corundum::entities
