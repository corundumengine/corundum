#pragma once
#include <array>
#include <corundum/core/containers/handle.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace corundum::core::memory {

  /** @brief Fixed-capacity generational object pool with O(1) acquire/release.
   *
   * Pre-allocates storage for @p MaxCount elements at construction time.
   * Acquire and release operate in O(1) via a free-list. No heap allocation
   * occurs after construction.
   *
   * Each slot carries a generation counter. Releasing a slot increments its
   * generation, which invalidates any outstanding Handle<T> referring to the
   * old occupant. Stale-handle detection via get() costs one integer compare.
   *
   * Slots start at generation 1 so a default-constructed Handle<T>
   * (generation == 0) is always invalid — no special null sentinel needed.
   *
   * @tparam T        Element type.
   * @tparam MaxCount Maximum number of live elements. Must be > 0.
   *
   * @pre T must be destructible and constructible from Args in acquire().
   * @thread_safety Not thread-safe. Caller must provide external synchronisation.
   *
   * @code
   *   Pool<Particle, 256> pool;
   *   auto h = pool.acquire(pos, vel);
   *   if (h) {
   *       Particle* p = pool.get(*h);
   *       // use p
   *       pool.release(*h);
   *   }
   * @endcode
   */
  template <typename T, std::size_t MaxCount> class Pool {
  public:
    using Handle = core::containers::Handle<T>;

    Pool() noexcept {
      for (std::size_t i = 0; i < MaxCount; ++i) {
        free_list_[i] = MaxCount - 1 - i;
        generations_[i] = 1; // generation 0 is the null sentinel; live slots start at 1
      }
      free_count_ = MaxCount;
    }

    /** @brief Acquire a slot, constructing @p Args in place.
     *  @return Handle to the new element, or std::unexpected if the pool is exhausted.
     *  @post On success, the handle remains valid until release() is called with it.
     */
    template <typename... Args>
    [[nodiscard]] std::expected<Handle, std::string_view>
    acquire(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
      if (free_count_ == 0)
        return std::unexpected("pool exhausted");
      const auto idx = free_list_[--free_count_];
      T *ptr = reinterpret_cast<T *>(&storage_[idx]);
      std::construct_at(ptr, std::forward<Args>(args)...);
      return Handle{static_cast<uint32_t>(idx), generations_[idx]};
    }

    /** @brief Dereference a handle.
     *  @return Pointer to the live element, or nullptr if @p h is null or stale.
     *  @performance O(1) — one bounds check + one generation compare.
     */
    [[nodiscard]] T *get(Handle h) noexcept {
      if (!h.valid() || h.index >= MaxCount || generations_[h.index] != h.generation)
        return nullptr;
      return reinterpret_cast<T *>(&storage_[h.index]);
    }

    /** @brief Dereference a handle (const overload).
     *  @return Const pointer to the live element, or nullptr if @p h is stale.
     */
    [[nodiscard]] const T *get(Handle h) const noexcept {
      if (!h.valid() || h.index >= MaxCount || generations_[h.index] != h.generation)
        return nullptr;
      return reinterpret_cast<const T *>(&storage_[h.index]);
    }

    /** @brief Release a previously acquired slot.
     *  @param[in] h  Handle returned by a prior acquire(). No-op if stale or null.
     *  @post The element is destroyed; its generation is incremented so all
     *        outstanding handles to this slot become stale.
     */
    void release(Handle h) noexcept {
      if (!h.valid() || h.index >= MaxCount || generations_[h.index] != h.generation)
        return;
      T *ptr = reinterpret_cast<T *>(&storage_[h.index]);
      std::destroy_at(ptr);
      ++generations_[h.index]; // invalidate outstanding handles
      free_list_[free_count_++] = h.index;
    }

    /// Number of currently live (acquired) elements.
    [[nodiscard]] std::size_t used() const noexcept {
      return MaxCount - free_count_;
    }

    /// Number of slots still available.
    [[nodiscard]] std::size_t available() const noexcept {
      return free_count_;
    }

    /// Maximum capacity.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
      return MaxCount;
    }

  private:
    alignas(alignof(T)) std::array<std::byte, MaxCount * sizeof(T)> storage_{};
    std::array<uint32_t, MaxCount> generations_{};
    std::array<std::size_t, MaxCount> free_list_{};
    std::size_t free_count_{};
  };

} // namespace corundum::core::memory
