#pragma once
#include <cstdint>

namespace corundum::core::containers {

  /** @brief Typed generational handle — the canonical reference type for pool-backed resources.
   *
   * Phantom type @p T prevents accidental mixing of handles to different resource kinds.
   * Generation tracking detects use-after-free: a slot's generation increments on release,
   * invalidating all outstanding handles that refer to the old occupant.
   *
   * A zero-generation handle is always invalid (the null state); live slots start at
   * generation 1, so a default-constructed Handle<T> is safely distinguishable.
   *
   * @tparam T  Tag type identifying the resource kind (need not be a complete type).
   *
   * @see corundum::core::memory::Pool<T> for the canonical backing store.
   * @see corundum::resources::handle for typed aliases used at subsystem boundaries.
   */
  template <typename T> struct Handle {
    uint32_t index{};
    uint32_t generation{};

    /** @brief Returns true when the handle was obtained from a live acquire().
     *  @note Valid does not mean live — the slot may have been released and reused.
     *        Always confirm via Pool::get() before dereferencing.
     */
    [[nodiscard]] bool valid() const noexcept {
      return generation != 0;
    }

    bool operator==(const Handle &) const = default;

    /** @brief Returns the canonical null (invalid) handle for this resource kind. */
    [[nodiscard]] static constexpr Handle null() noexcept {
      return {};
    }
  };

} // namespace corundum::core::containers
