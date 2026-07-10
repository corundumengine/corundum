#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/entity/entity.hpp>
#include <cstdint>
#include <limits>
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
    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index: EntityId → dense row ─────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Cold: debug labels, never read in Update() ─────────────────
    std::array<std::array<char, 64>, k_max> name{};
    std::array<std::size_t, k_max> name_len{};

    std::uint32_t count = 0;

    TransformNameTable() noexcept {
      sparse.fill(k_invalid);
    }

    /** @brief True if @p e has a name entry. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto i = e.index;
      if (i >= k_max)
        return false;
      const auto s = sparse[i];
      return s != k_invalid && entities[s] == e;
    }

    /** @brief Add an empty name row for @p e.
     *  @param[in] e Entity (must not already be present). @pre has(e) must be false.
     */
    void insert(EntityId e) noexcept {
      assert(!has(e));
      const auto idx = e.index;
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      name[slot][0] = '\0';
      name_len[slot] = 0;
      ++count;
    }

    /** @brief Remove @p e's name row via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      assert(has(e));
      const auto idx = e.index;
      const auto slot = sparse[idx];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[last_e.index] = slot;
        entities[slot] = last_e;
        name[slot] = name[last];
        name_len[slot] = name_len[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Set the debug label for @p e (truncated to 63 chars).
     *  @param[in] e Entity to label. @pre has(e) must be true.
     *  @param[in] n Label string.
     */
    void set(EntityId e, std::string_view n) noexcept {
      assert(has(e));
      const auto slot = sparse[e.index];
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
      const auto slot = sparse[e.index];
      return std::string_view(name[slot].data(), name_len[slot]);
    }
  };

} // namespace corundum::gameplay::component
