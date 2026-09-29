// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <corundum/core/verify.hpp>
#include <corundum/entities/entity.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace corundum::entities {

  /// Sparse-index bookkeeping shared by all component tables.
  /// Owns the sparse map and dense entity array. The has/insert/remove spine
  /// logic is extracted here once; payload-specific reads and writes are
  /// supplied via lambdas at the call site.  Each table retains its own
  /// uint32_t count member (GameTable concept requires t.count).
  template <std::uint32_t KMax = k_max_entities> struct SparseIndex {
    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    std::array<std::uint32_t, KMax> sparse{};
    std::array<EntityId, KMax> entities{};

    SparseIndex() noexcept {
      sparse.fill(k_invalid);
    }

    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto i = e.index;
      if (i >= KMax)
        return false;
      const auto s = sparse[i];
      return s != k_invalid && entities[s] == e;
    }

    /** @brief Dense slot of @p e.
     *  @pre has(e) must be true; enforced in every build type.
     *  @return Index into this table's dense payload arrays. */
    [[nodiscard]] std::uint32_t dense_index(EntityId e) const noexcept {
      const bool present = has(e);
      core::verify(present, "SparseIndex::dense_index: stale or foreign EntityId");
      return sparse[e.index];
    }

    /** @brief Dense slot of @p e, or nullopt when @p e is stale, foreign or absent. Never aborts. */
    [[nodiscard]] std::optional<std::uint32_t> try_dense_index(EntityId e) const noexcept {
      if (!has(e))
        return std::nullopt;
      return sparse[e.index];
    }

    [[nodiscard]] auto active_entities(this auto &self, std::uint32_t count) noexcept {
      return std::span(self.entities).first(count);
    }

    /// Insert spine: allocates a dense slot, links the sparse index, then
    /// calls @p write(slot) to let the table initialise its payload.
    /// @p count is the table's own count member (incremented after the write).
    /// @pre @p e's index is in range and @p e is not already present; both enforced in
    ///      every build type.
    template <typename Fn> void insert(EntityId e, std::uint32_t &count, Fn &&write) noexcept {
      core::verify(e.index < KMax, "SparseIndex::insert: EntityId index out of range");
      core::verify(!has(e), "SparseIndex::insert: EntityId already present");
      const auto i = e.index;
      const auto slot = count;
      sparse[i] = slot;
      entities[slot] = e;
      std::forward<Fn>(write)(slot);
      ++count;
    }

    /// Remove spine: swap-and-pop. Calls @p swap(slot, last) so the table
    /// can copy its own payload arrays, then invalidates the removed slot.
    /// @p count is the table's own count member (decremented after the swap).
    /// @pre has(e) must be true; enforced in every build type.
    template <typename Fn> void remove(EntityId e, std::uint32_t &count, Fn &&swap) noexcept {
      core::verify(has(e), "SparseIndex::remove: EntityId not present");
      const auto i = e.index;
      const auto slot = sparse[i];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[last_e.index] = slot;
        entities[slot] = last_e;
        std::forward<Fn>(swap)(slot, last);
      }
      sparse[i] = k_invalid;
      --count;
    }
  };

} // namespace corundum::entities
