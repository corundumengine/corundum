#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <cstdint>
#include <string_view>

namespace corundum::gameplay::component {

  /** @brief Cold debug-label table for transforms.
   *
   * Spawned alongside TransformTable but never read during physics or animation
   * updates. Split from TransformTable to keep the hot float arrays on their own
   * cache lines. Access by entity ID:
   * @code
   *   names.set(e, "player");
   *   auto label = names.get(e);
   * @endcode
   */
  struct TransformNameTable {
    static constexpr auto k_max = k_max_entities;

    // ── Sparse index: EntityId → dense row ─────────────────────────
    SparseIndex<k_max> idx;

    // ── Cold: debug labels, never read in Update() ─────────────────
    std::array<std::array<char, 64>, k_max> name{};
    std::array<std::size_t, k_max> name_len{};

    std::uint32_t count = 0;

    /** @brief True if @p e has a name entry. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add an empty name row for @p e.
     *  @param[in] e Entity (must not already be present). @pre has(e) must be false.
     */
    void insert(EntityId e) noexcept {
      idx.insert(e, count, [&](auto slot) {
        name[slot][0] = '\0';
        name_len[slot] = 0;
      });
    }

    /** @brief Remove @p e's name row via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) {
        name[slot] = name[last];
        name_len[slot] = name_len[last];
      });
    }

    /** @brief Set the debug label for @p e (truncated to 63 chars).
     *  @param[in] e Entity to label. @pre has(e) must be true.
     *  @param[in] n Label string.
     */
    void set(EntityId e, std::string_view n) noexcept {
      assert(has(e));
      const auto slot = idx.dense_idx(e);
      const auto len = std::min(n.size(), std::size_t{63});
      std::copy_n(n.data(), len, name[slot].data());
      name[slot][len] = '\0';
      name_len[slot] = len;
    }

    /** @brief Debug label for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e) must be true.
     *  @return Non-owning view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get(EntityId e) const noexcept {
      assert(has(e));
      const auto slot = idx.dense_idx(e);
      return std::string_view(name[slot].data(), name_len[slot]);
    }
  };

} // namespace corundum::gameplay::component
