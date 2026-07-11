#pragma once
#include <cassert>
#include <corundum/gameplay/entity/entity.hpp>
#include <limits>
#include <span>

namespace corundum::gameplay::component {

  using corundum::gameplay::entity::EntityId;
  using corundum::gameplay::entity::k_max_entities;

  /// Sparse-index bookkeeping shared by all component tables.
  /// Owns the sparse map and dense entity array. The has/insert/remove spine
  /// logic is extracted here once; payload-specific reads and writes are
  /// supplied via lambdas at the call site.  Each table retains its own
  /// uint32_t count member (GameTable concept requires t.count).
  template <std::uint32_t KMax = k_max_entities>
  struct SparseIndex {
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

    [[nodiscard]] std::uint32_t dense_idx(EntityId e) const noexcept {
      assert(has(e));
      return sparse[e.index];
    }

    [[nodiscard]] auto active_entities(this auto &self, std::uint32_t count) noexcept {
      return std::span(self.entities).first(count);
    }

    /// Insert spine: allocates a dense slot, links the sparse index, then
    /// calls @p write(slot) to let the table initialise its payload.
    /// @p count is the table's own count member (incremented after the write).
    template <typename Fn>
    void insert(EntityId e, std::uint32_t &count, Fn &&write) noexcept {
      assert(!has(e));
      const auto i = e.index;
      const auto slot = count;
      sparse[i] = slot;
      entities[slot] = e;
      write(slot);
      ++count;
    }

    /// Remove spine: swap-and-pop. Calls @p swap(slot, last) so the table
    /// can copy its own payload arrays, then invalidates the removed slot.
    /// @p count is the table's own count member (decremented after the swap).
    template <typename Fn>
    void remove(EntityId e, std::uint32_t &count, Fn &&swap) noexcept {
      assert(has(e));
      const auto i = e.index;
      const auto slot = sparse[i];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[last_e.index] = slot;
        entities[slot] = last_e;
        swap(slot, last);
      }
      sparse[i] = k_invalid;
      --count;
    }
  };

} // namespace corundum::gameplay::component
