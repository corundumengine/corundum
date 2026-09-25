// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/core/verify.hpp>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <ranges>

namespace corundum::entities {

  inline constexpr std::uint32_t k_max_entities = 1024;

  /// Portable L1 cache line size in bytes (correct for x86-64 and most ARM parts; still a
  /// valid, if not maximal, alignment on targets with wider lines such as Apple Silicon's
  /// 128-byte L1). Hot SoA component arrays should align to this, not the SSE-era 16-byte
  /// constant.
  inline constexpr std::size_t k_cache_line = 64;

  /** @brief Generational entity handle.
   *
   * A non-zero generation distinguishes a live handle from a default-constructed
   * (invalid) one.  After an entity is destroyed its generation increments so that
   * all outstanding handles become stale — detectable via `EntityManager::is_live()`
   * or by a generation-aware `Table::has()`.
   */
  struct EntityId {
    std::uint32_t index{};
    std::uint32_t generation{};

    [[nodiscard]] constexpr bool valid() const noexcept {
      return generation != 0;
    }

    bool operator==(const EntityId &) const = default;

    [[nodiscard]] static constexpr EntityId invalid() noexcept {
      return {};
    }
  };

  /** @brief Free-list pool for entity IDs; O(1) create and destroy.
   *
   * Maintains a stack-based free list of recycled IDs.  Each slot has an associated
   * generation counter that increments on every `destroy()` call, so handles that
   * outlive their entity are rejectable.  The free list, generation table, and free
   * count are private — the pool's invariant holds only while those change through
   * create() and destroy().
   */
  class EntityManager {
  public:
    /** @brief Initialises the pool so create() hands out IDs 0, 1, 2, … */
    EntityManager() noexcept {
      std::ranges::iota(std::views::reverse(free_list_), std::uint32_t{0});
      generations_.fill(1);
    }

    /** @brief Allocate a fresh entity ID.
     *  @pre Pool must not be full — check full() first. Enforced in every build type: exhausting the
     *       pool aborts rather than reading past the free list.
     *  @return A live entity ID with the current generation for the recyclable slot.
     */
    [[nodiscard]] EntityId create() noexcept {
      core::verify(free_count_ > 0, "entity pool exhausted (k_max_entities)");
      const std::uint32_t idx = free_list_[--free_count_];
      return {.index = idx, .generation = generations_[idx]};
    }

    /** @brief Return id to the free list for reuse.
     *
     *  If id's generation does not match the current generation for its slot the
     *  call is a no-op (the entity was already destroyed or the handle is stale).
     *  This makes double-destroy safe at the cost of a generation check.
     *
     *  @param[in] id Entity handle returned by create().
     */
    void destroy(EntityId id) noexcept {
      assert(id.index < k_max_entities && "Entity ID out of range");
      if (id.generation != generations_[id.index])
        return; // Already destroyed or stale handle — no-op.
      assert(free_count_ < k_max_entities && "Double-destroy detected");
      ++generations_[id.index];
      free_list_[free_count_++] = id.index;
    }

    /** @brief True if @p id refers to the current occupant of its slot.
     *  @param[in] id Entity handle to query. */
    [[nodiscard]] bool is_live(EntityId id) const noexcept {
      return id.valid() && id.index < k_max_entities && id.generation == generations_[id.index];
    }

    /** @brief True when the pool is exhausted and create() would assert. */
    [[nodiscard]] bool full() const noexcept {
      return free_count_ == 0;
    }

    /** @brief Number of currently live entities; O(1). */
    [[nodiscard]] std::uint32_t alive() const noexcept {
      return k_max_entities - free_count_;
    }

  private:
    /** @brief Free list stores raw indices. */
    std::array<std::uint32_t, k_max_entities> free_list_{};

    /** @brief Per-slot generation counter; incremented on each destroy(). */
    std::array<std::uint32_t, k_max_entities> generations_{};

    /** @brief Number of available (free) slots. */
    std::uint32_t free_count_ = k_max_entities;
  };

} // namespace corundum::entities
