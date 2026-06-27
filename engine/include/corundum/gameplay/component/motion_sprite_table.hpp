#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/resources/sprite.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::component {

  /** @brief SoA table for entities with motion-driven sprite sheet switching.
   *
   * Stores a walk sprite and an idle sprite (plus pre-cached per-direction frame
   * counts for each) for entities whose visible animation depends on whether they
   * are moving. Configurable per-direction transition delays prevent flickering
   * on brief inputs or mid-step stops.
   *
   * Transition logic (handled by AnimationSystem::animate):
   *   - When desired sprite differs from current, a pending transition starts.
   *   - The transition commits only when the pending timer >= the configured delay.
   *   - If the desired sprite reverts to current before the timer fires, the
   *     pending transition is cancelled and the timer resets.
   *
   * @note Only entities with both a walk sheet and an idle sheet need an entry.
   */
  struct MotionSpriteTable {
    static constexpr auto k_max = k_max_entities;
    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Sprite IDs and cached frame counts (set once at spawn) ─────
    std::array<corundum::resources::SpriteId, k_max> walk_id{};
    std::array<corundum::resources::SpriteId, k_max> idle_id{};

    // Flat frame-count caches: index = slot * k_num_anim_ids + AnimId.
    std::array<uint8_t, k_max * corundum::resources::k_num_anim_ids> walk_counts{};
    std::array<uint8_t, k_max * corundum::resources::k_num_anim_ids> idle_counts{};

    // ── Transition delay config (seconds; set once at spawn) ───────
    std::array<float, k_max> idle_to_walk_delay{}; ///< Delay before idle→walk commits.
    std::array<float, k_max> walk_to_idle_delay{}; ///< Delay before walk→idle commits.

    // ── Per-sprite frame duration overrides (0 = use AnimationTable default) ─
    std::array<float, k_max> walk_frame_duration{}; ///< Seconds per frame while walking; 0 = no override.
    std::array<float, k_max> idle_frame_duration{}; ///< Seconds per frame while idle; 0 = no override.

    // ── Pending transition runtime state (mutated by AnimationSystem) ─
    /// SpriteId the entity is transitioning toward; k_null_sprite_id = no pending transition.
    std::array<corundum::resources::SpriteId, k_max> pending_sid{};
    std::array<float, k_max> transition_timer{}; ///< Accumulated time toward the pending transition.

    std::uint32_t count = 0;

    MotionSpriteTable() noexcept {
      sparse.fill(k_invalid);
    }

    // ── Queries ────────────────────────────────────────────────────

    /** @brief True if @p e has a motion-sprite entry. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Walk SpriteId for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::SpriteId walk_sprite(EntityId e) const noexcept {
      return walk_id[sparse[std::to_underlying(e)]];
    }

    /** @brief Idle SpriteId for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::SpriteId idle_sprite(EntityId e) const noexcept {
      return idle_id[sparse[std::to_underlying(e)]];
    }

    /** @brief Configured delay (seconds) for the transition to @p target_sid. @pre has(e). */
    [[nodiscard]] float delay_for(EntityId e, corundum::resources::SpriteId target_sid) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      return (target_sid == walk_id[slot]) ? idle_to_walk_delay[slot] : walk_to_idle_delay[slot];
    }

    /** @brief Frame duration override (seconds/frame) for @p target_sid; 0 means use AnimationTable default. @pre
     * has(e). */
    [[nodiscard]] float frame_duration_for(EntityId e, corundum::resources::SpriteId target_sid) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      return (target_sid == walk_id[slot]) ? walk_frame_duration[slot] : idle_frame_duration[slot];
    }

    /** @brief SpriteId currently pending transition, or k_null_sprite_id if none. @pre has(e). */
    [[nodiscard]] corundum::resources::SpriteId pending_sprite(EntityId e) const noexcept {
      return pending_sid[sparse[std::to_underlying(e)]];
    }

    /** @brief Walk frame counts for @p e, suitable for AnimationTable::set_frame_counts. @pre has(e). */
    [[nodiscard]] std::array<uint8_t, corundum::resources::k_num_anim_ids>
    walk_frame_counts(EntityId e) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      std::array<uint8_t, corundum::resources::k_num_anim_ids> out;
      std::copy_n(&walk_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids],
                  corundum::resources::k_num_anim_ids, out.data());
      return out;
    }

    /** @brief Idle frame counts for @p e, suitable for AnimationTable::set_frame_counts. @pre has(e). */
    [[nodiscard]] std::array<uint8_t, corundum::resources::k_num_anim_ids>
    idle_frame_counts(EntityId e) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      std::array<uint8_t, corundum::resources::k_num_anim_ids> out;
      std::copy_n(&idle_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids],
                  corundum::resources::k_num_anim_ids, out.data());
      return out;
    }

    // ── Transition mutations (called by AnimationSystem) ───────────

    /** @brief Begin tracking a transition to @p target, resetting the timer.
     *  No-op if @p target is already the pending target.
     *  @pre has(e).
     */
    void set_pending(EntityId e, corundum::resources::SpriteId target) noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      pending_sid[slot] = target;
      transition_timer[slot] = 0.f;
    }

    /** @brief Advance the transition timer by @p dt seconds and return the new total.
     *  @pre has(e).
     */
    float tick_transition(EntityId e, float dt) noexcept {
      auto &t = transition_timer[sparse[std::to_underlying(e)]];
      t += dt;
      return t;
    }

    /** @brief Cancel any pending transition, resetting the timer to zero. @pre has(e). */
    void cancel_transition(EntityId e) noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      pending_sid[slot] = corundum::resources::k_null_sprite_id;
      transition_timer[slot] = 0.f;
    }

    // ── Spawn / despawn mutations ──────────────────────────────────

    /** @brief Add a motion-sprite entry for @p e.
     *  @param[in] wid          Walk sheet SpriteId.
     *  @param[in] iid          Idle sheet SpriteId.
     *  @param[in] wc           Frame counts for the walk sheet, indexed by AnimId.
     *  @param[in] ic           Frame counts for the idle sheet, indexed by AnimId.
     *  @param[in] itw_delay    Seconds to wait before idle→walk commits (default 0).
     *  @param[in] wti_delay    Seconds to wait before walk→idle commits (default 0).
     *  @param[in] walk_fd      Seconds per frame for the walk sheet; 0 = use AnimationTable default.
     *  @param[in] idle_fd      Seconds per frame for the idle sheet; 0 = use AnimationTable default.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, corundum::resources::SpriteId wid, corundum::resources::SpriteId iid,
                const std::array<uint8_t, corundum::resources::k_num_anim_ids> &wc,
                const std::array<uint8_t, corundum::resources::k_num_anim_ids> &ic, float itw_delay = 0.f,
                float wti_delay = 0.f, float walk_fd = 0.f, float idle_fd = 0.f) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      walk_id[slot] = wid;
      idle_id[slot] = iid;
      idle_to_walk_delay[slot] = itw_delay;
      walk_to_idle_delay[slot] = wti_delay;
      walk_frame_duration[slot] = walk_fd;
      idle_frame_duration[slot] = idle_fd;
      pending_sid[slot] = corundum::resources::k_null_sprite_id;
      transition_timer[slot] = 0.f;
      std::copy_n(wc.data(), corundum::resources::k_num_anim_ids,
                  &walk_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids]);
      std::copy_n(ic.data(), corundum::resources::k_num_anim_ids,
                  &idle_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids]);
      ++count;
    }

    /** @brief Remove @p e's entry via swap-and-pop. @pre has(e). */
    void remove(EntityId e) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = sparse[idx];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[std::to_underlying(last_e)] = slot;
        entities[slot] = last_e;
        walk_id[slot] = walk_id[last];
        idle_id[slot] = idle_id[last];
        idle_to_walk_delay[slot] = idle_to_walk_delay[last];
        walk_to_idle_delay[slot] = walk_to_idle_delay[last];
        walk_frame_duration[slot] = walk_frame_duration[last];
        idle_frame_duration[slot] = idle_frame_duration[last];
        pending_sid[slot] = pending_sid[last];
        transition_timer[slot] = transition_timer[last];
        const auto wdst = static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids;
        const auto wsrc = static_cast<std::size_t>(last) * corundum::resources::k_num_anim_ids;
        std::copy_n(&walk_counts[wsrc], corundum::resources::k_num_anim_ids, &walk_counts[wdst]);
        std::copy_n(&idle_counts[wsrc], corundum::resources::k_num_anim_ids, &idle_counts[wdst]);
      }
      sparse[idx] = k_invalid;
      --count;
    }

    // ── GameTable concept compliance ───────────────────────────────

    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.walk_id).first(self.count);
    }

    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }
  };

  static_assert(GameTable<MotionSpriteTable>);

} // namespace corundum::gameplay::component
