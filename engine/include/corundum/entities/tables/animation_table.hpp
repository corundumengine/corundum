// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstddef>
#include <cstdint>
#include <span>

namespace corundum::entities {

/** Frame duration used when an animation row is inserted without an authored rate. */
inline constexpr float k_default_frame_duration = 0.15f;

/** SoA table for the Animation component.
 *
 *  Only entities with animated sprites appear here (player, NPCs). Static scenery
 *  has no entry. `frame_counts` is a flat 2D buffer `[k_max × k_num_anim_ids]`; use
 *  `frame_count(e, anim_id)` for lookup. `timer` and `frame_duration` are
 *  `alignas(k_cache_line)` and front-loaded so they never share a cache line with
 *  — or sit immediately before — the cold data below.
 */
struct AnimationTable {
  static constexpr auto k_max = k_max_entities;

  alignas(k_cache_line) std::array<float, k_max> timer{};

  alignas(k_cache_line) std::array<float, k_max> frame_duration{};

  SparseIndex<k_max> index;

  /** frame_counts[dense_slot * k_num_anim_ids + anim_id]; read on animation
   *  transition, cached at spawn from character registry.
   */
  std::array<uint8_t, static_cast<std::size_t>(k_max) * sprites::k_num_anim_ids> frame_counts{};

  std::uint32_t count = 0;

  /** Contiguous span over playback timers for all live entities. */
  [[nodiscard]] auto active_timer(this auto &self) noexcept { return std::span(self.timer).first(self.count); }

  /** Span over timer (alias for GameTable concept compliance). */
  [[nodiscard]] auto active_span(this auto &self) noexcept { return std::span(self.timer).first(self.count); }

  /** Contiguous span of EntityIds in dense order. */
  [[nodiscard]] auto active_entities(this auto &self) noexcept { return self.index.active_entities(self.count); }

  /** True if `e` has an animation row.
   *  @param e Entity to query.
   */
  [[nodiscard]] bool has(EntityId e) const noexcept { return index.has(e); }

  /** Add an animation row for `e`.
   *  @param e                 Entity (must not already be present).
   *  @param seconds_per_frame Authored seconds per frame; non-positive uses the default.
   *  @pre has(e) must be false.
   */
  void insert(EntityId e, float seconds_per_frame = k_default_frame_duration) noexcept {
    index.insert(e, count, [&](auto slot) {
      timer[slot] = 0.f;
      frame_duration[slot] = seconds_per_frame > 0.f ? seconds_per_frame : k_default_frame_duration;
      auto *counts = &frame_counts[static_cast<std::size_t>(slot) * sprites::k_num_anim_ids];
      std::fill_n(counts, sprites::k_num_anim_ids, uint8_t{0});
    });
  }

  /** Remove `e`'s animation row via swap-and-pop.
   *  @param e Entity to remove.
   *  @pre has(e) must be true.
   */
  void remove(EntityId e) noexcept {
    index.remove(e, count, [&](auto slot, auto last) {
      timer[slot] = timer[last];
      frame_duration[slot] = frame_duration[last];
      auto *destination = &frame_counts[static_cast<std::size_t>(slot) * sprites::k_num_anim_ids];
      const auto *source = &frame_counts[static_cast<std::size_t>(last) * sprites::k_num_anim_ids];
      std::copy_n(source, sprites::k_num_anim_ids, destination);
    });
  }

  /** Mutable playback timer reference for `e`. @pre has(e). */
  [[nodiscard]] float &timer_ref(EntityId e) noexcept {
    assert(has(e));
    return timer[index.dense_index(e)];
  }

  /** Mutable frame duration reference for `e`. @pre has(e). */
  [[nodiscard]] float &frame_duration_ref(EntityId e) noexcept {
    assert(has(e));
    return frame_duration[index.dense_index(e)];
  }

  /** Number of frames in `anim_id` for entity `e`; 0 if the clip is absent.
   *  @param e      Entity to query.
   *  @param anim_id Animation clip ID.
   *  @pre has(e) must be true.
   *  @pre anim_id must not be AnimId::Count (a sentinel, not a real clip).
   */
  [[nodiscard]] uint8_t frame_count(EntityId e, sprites::AnimId anim_id) const noexcept {
    assert(has(e));
    assert(anim_id != sprites::AnimId::Count); // Count is a sentinel, not a real clip.
    const auto slot = index.dense_index(e);
    return frame_counts[(static_cast<std::size_t>(slot) * sprites::k_num_anim_ids) + static_cast<uint8_t>(anim_id)];
  }

  /** All of `e`'s per-clip frame counts, indexed by AnimId. @pre has(e). */
  [[nodiscard]] std::array<uint8_t, sprites::k_num_anim_ids> frame_counts_of(EntityId e) const noexcept {
    assert(has(e));
    const auto slot = index.dense_index(e);
    std::array<uint8_t, sprites::k_num_anim_ids> counts{};
    std::copy_n(&frame_counts[static_cast<std::size_t>(slot) * sprites::k_num_anim_ids], sprites::k_num_anim_ids,
                counts.begin());
    return counts;
  }

  /** Bulk-set all per-clip frame counts for `e` (called once at spawn).
   *  @param e      Entity whose counts to write.
   *  @param counts Array indexed by AnimId; 0 means the clip is absent.
   *  @pre has(e) must be true.
   */
  void set_frame_counts(EntityId e, const std::array<uint8_t, sprites::k_num_anim_ids> &counts) noexcept {
    assert(has(e));
    const auto slot = index.dense_index(e);
    auto *destination = &frame_counts[static_cast<std::size_t>(slot) * sprites::k_num_anim_ids];
    std::copy_n(counts.data(), sprites::k_num_anim_ids, destination);
  }
};

static_assert(GameTable<AnimationTable>);

} // namespace corundum::entities
