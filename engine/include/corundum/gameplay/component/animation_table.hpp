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

  /** @brief SoA table for the Animation component.
   *
   * Only entities with animated sprites appear here (player, NPCs). Static scenery
   * has no entry. `frame_counts` is a flat 2D buffer `[k_max × k_num_anim_ids]`; use
   * `frame_count(e, anim_id)` for lookup. `timer` and `frame_duration` are
   * `alignas(16)` for auto-vectorisation.
   */
  struct AnimationTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Hot: timer / frame_duration tick every frame ───────────────
    alignas(16) std::array<float, k_max> timer{};
    alignas(16) std::array<float, k_max> frame_duration{};

    // ── Lukewarm: frame_counts[entity_idx * k_num_anim_ids + anim_id] ──
    // Read on animation transition; cached at spawn from character registry.
    std::array<uint8_t, static_cast<std::size_t>(k_max) * corundum::resources::k_num_anim_ids> frame_counts{};

    std::uint32_t count = 0;

    AnimationTable() noexcept {
      sparse.fill(k_invalid);
      // Default frame_duration
      std::fill(frame_duration.begin(), frame_duration.end(), 0.15f);
    }

    /** @brief Contiguous span over playback timers for all live entities. */
    [[nodiscard]] auto active_timer(this auto &self) noexcept {
      return std::span(self.timer).first(self.count);
    }

    /** @brief Span over timer (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.timer).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has an animation row.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add an animation row for @p e with default 0.15 s frame duration.
     *  @param[in] e Entity (must not already be present).
     *  @pre has(e) must be false.
     */
    void insert(EntityId e) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      timer[slot] = 0.f;
      frame_duration[slot] = 0.15f;
      // Zero the frame_counts row
      auto *fc = &frame_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids];
      std::fill_n(fc, corundum::resources::k_num_anim_ids, uint8_t{0});
      ++count;
    }

    /** @brief Remove @p e's animation row via swap-and-pop.
     *  @param[in] e Entity to remove.
     *  @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = sparse[idx];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[std::to_underlying(last_e)] = slot;
        entities[slot] = last_e;
        timer[slot] = timer[last];
        frame_duration[slot] = frame_duration[last];
        // Copy the frame_counts row
        auto *dst = &frame_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids];
        const auto *src = &frame_counts[static_cast<std::size_t>(last) * corundum::resources::k_num_anim_ids];
        std::copy_n(src, corundum::resources::k_num_anim_ids, dst);
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Mutable playback timer reference for @p e. @pre has(e). */
    [[nodiscard]] float &timer_ref(EntityId e) noexcept {
      return timer[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable frame duration reference for @p e. @pre has(e). */
    [[nodiscard]] float &frame_duration_ref(EntityId e) noexcept {
      return frame_duration[sparse[std::to_underlying(e)]];
    }

    /** @brief Number of frames in @p aid for entity @p e; 0 if the clip is absent.
     *  @param[in] e   Entity to query.
     *  @param[in] aid Animation clip ID.
     *  @pre has(e) must be true.
     */
    [[nodiscard]] uint8_t frame_count(EntityId e, corundum::resources::AnimId aid) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      return frame_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids +
                          static_cast<uint8_t>(aid)];
    }

    /** @brief Bulk-set all per-clip frame counts for @p e (called once at spawn).
     *  @param[in] e      Entity whose counts to write.
     *  @param[in] counts Array indexed by AnimId; 0 means the clip is absent.
     *  @pre has(e) must be true.
     */
    void set_frame_counts(EntityId e, const std::array<uint8_t, corundum::resources::k_num_anim_ids> &counts) noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      auto *dst = &frame_counts[static_cast<std::size_t>(slot) * corundum::resources::k_num_anim_ids];
      std::copy_n(counts.data(), corundum::resources::k_num_anim_ids, dst);
    }
  };

  static_assert(GameTable<AnimationTable>);

} // namespace corundum::gameplay::component
