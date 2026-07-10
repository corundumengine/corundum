#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <numeric>

namespace corundum::gameplay::entity {

  inline constexpr std::uint32_t k_max_entities = 256;

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
   * outlive their entity are rejectable.
   */
  struct EntityManager {
    /** @brief Free list stores raw indices. */
    std::array<std::uint32_t, k_max_entities> free_list_raw;

    /** @brief Per-slot generation counter; incremented on each destroy(). */
    std::array<std::uint32_t, k_max_entities> generations{};

    /** @brief Number of available (free) slots. */
    std::uint32_t free_count = k_max_entities;

    /** @brief Initialises the pool so create() hands out IDs 0, 1, 2, … */
    EntityManager() noexcept {
      std::iota(free_list_raw.rbegin(), free_list_raw.rend(), std::uint32_t{0});
      generations.fill(1);
    }

    /** @brief Allocate a fresh entity ID.
     *  @pre Pool must not be full — check full() first.
     *  @return A live entity ID with the current generation for the recyclable slot.
     */
    [[nodiscard]] EntityId create() noexcept {
      assert(free_count > 0 && "Entity pool exhausted");
      const std::uint32_t idx = free_list_raw[--free_count];
      return {idx, generations[idx]};
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
      if (id.generation != generations[id.index])
        return; // Already destroyed or stale handle — no-op.
      assert(free_count < k_max_entities && "Double-destroy detected");
      ++generations[id.index];
      free_list_raw[free_count++] = id.index;
    }

    /** @brief True if @p id refers to the current occupant of its slot.
     *  @param[in] id Entity handle to query. */
    [[nodiscard]] bool is_live(EntityId id) const noexcept {
      return id.valid() && id.index < k_max_entities && id.generation == generations[id.index];
    }

    /** @brief True when the pool is exhausted and create() would assert. */
    [[nodiscard]] bool full() const noexcept {
      return free_count == 0;
    }

    /** @brief Number of currently live entities; O(1). */
    [[nodiscard]] std::uint32_t alive() const noexcept {
      return k_max_entities - free_count;
    }
  };

} // namespace corundum::gameplay::entity
