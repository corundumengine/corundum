#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>

namespace corundum::gameplay::ecs {

  inline constexpr std::uint32_t k_max_entities = 256;

  /** @brief Strongly-typed entity handle.
   *
   * All array index conversions must use `std::to_underlying(id)`.
   * This prevents accidental arithmetic and makes the conversion visible.
   */
  enum class EntityId : std::uint32_t { Invalid = std::numeric_limits<std::uint32_t>::max() };

  /** @brief Free-list pool for entity IDs; O(1) create and destroy.
   *
   * Maintains a stack-based free list of recycled IDs. allocate() pops from
   * the top and destroy() pushes back. No heap allocation — the free list is
   * a fixed-size std::array.
   */
  struct EntityManager {
    /** @brief Internal free list stores raw indices; converted to/from EntityId
     *         at the API boundary. */
    std::array<std::uint32_t, k_max_entities> free_list_raw;

    /** @brief Number of available (free) slots. */
    std::uint32_t free_count = k_max_entities;

    /** @brief Initialises the pool so create() hands out IDs 0, 1, 2, … */
    EntityManager() noexcept {
      std::iota(free_list_raw.rbegin(), free_list_raw.rend(), std::uint32_t{0});
    }

    /** @brief Allocate a fresh entity ID.
     *  @pre Pool must not be full — check full() first.
     *  @return A live entity ID; recycles IDs returned by destroy().
     */
    [[nodiscard]] EntityId create() noexcept {
      assert(free_count > 0 && "Entity pool exhausted");
      return static_cast<EntityId>(free_list_raw[--free_count]);
    }

    /** @brief Return id to the free list for reuse.
     *  @param[in] id A live entity (returned by create() and not yet destroyed).
     *  @pre id must be a live entity.
     */
    void destroy(EntityId id) noexcept {
      const auto idx = std::to_underlying(id);
      assert(idx < k_max_entities && "Entity ID out of range");
      assert(free_count < k_max_entities && "Double-destroy detected");
      free_list_raw[free_count++] = idx;
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

} // namespace corundum::gameplay::ecs
