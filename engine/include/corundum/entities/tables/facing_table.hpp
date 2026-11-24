// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/core/direction.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <cstdint>
#include <span>

namespace corundum::entities {

/** SoA table for the Facing component.
 *
 *  Only entities with directional behaviour (player, some NPCs) have an entry.
 *  Facing is updated by the animation system when the entity moves.
 */
struct FacingTable {
  static constexpr auto k_max = k_max_entities;

  SparseIndex<k_max> index;

  std::array<core::Direction, k_max> dir{};

  std::uint32_t count = 0;

  /** Contiguous span over facing directions for all live entities. */
  [[nodiscard]] auto active_dir(this auto &self) noexcept { return std::span(self.dir).first(self.count); }

  /** Span over dir (alias for GameTable concept compliance). */
  [[nodiscard]] auto active_span(this auto &self) noexcept { return std::span(self.dir).first(self.count); }

  /** Contiguous span of EntityIds in dense order. */
  [[nodiscard]] auto active_entities(this auto &self) noexcept { return self.index.active_entities(self.count); }

  /** True if `e` has a facing entry. @param e Entity to query. */
  [[nodiscard]] bool has(EntityId e) const noexcept { return index.has(e); }

  /** Add a facing row for `e`.
   *  @param e Entity (must not already be present).
   *  @param d Initial facing direction.
   *  @pre has(e) must be false.
   */
  void insert(EntityId e, core::Direction d) noexcept {
    index.insert(e, count, [&](auto slot) { dir[slot] = d; });
  }

  /** Remove `e`'s facing row via swap-and-pop.
   *  @param e Entity to remove. @pre has(e) must be true.
   */
  void remove(EntityId e) noexcept {
    index.remove(e, count, [&](auto slot, auto last) { dir[slot] = dir[last]; });
  }

  /** Mutable facing direction reference for `e`. @pre has(e). */
  [[nodiscard]] core::Direction &dir_ref(EntityId e) noexcept {
    assert(has(e));
    return dir[index.dense_index(e)];
  }

  /** Facing direction of `e`. @pre has(e). */
  [[nodiscard]] core::Direction dir_of(EntityId e) const noexcept {
    assert(has(e));
    return dir[index.dense_index(e)];
  }
};

static_assert(GameTable<FacingTable>);

} // namespace corundum::entities
