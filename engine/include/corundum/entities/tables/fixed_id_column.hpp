// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>

namespace corundum::entities {

  /** @brief Shared storage for a fixed-capacity string column keyed by EntityId.
   *
   * Backs the tables that map an entity to a stable authoring id (actor ids,
   * dialogue graph ids). Each id lives in a fixed-size `char[]` buffer so the
   * derived table stays trivially copyable (required by the GameTable concept);
   * ids longer than @ref k_max_id_len are truncated. The sparse/dense spine and
   * the swap-and-pop bookkeeping live here once — derive and add domain-named
   * accessors over the protected helpers.
   */
  template <std::uint32_t KMax> struct FixedIdColumn {
    static constexpr std::size_t k_max_id_len = 128;

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<KMax> index;

    // ── Id strings (cold — read only on quest/save resolution) ─────
    std::array<std::array<char, k_max_id_len>, KMax> ids{};

    std::array<std::size_t, KMax> id_lengths{};

    std::uint32_t count = 0;

    /** @brief Span over id buffers (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.ids).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return self.index.active_entities(self.count);
    }

    /** @brief True if @p e has an id row. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return index.has(e);
    }

    /** @brief Add an id row for @p e.
     *  @param[in] e  Entity (must not already be present).
     *  @param[in] id Id string (truncated to k_max_id_len-1).
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, std::string_view id) noexcept {
      index.insert(e, count, [&](auto slot) { write(slot, id); });
    }

    /** @brief Remove @p e's id row via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      index.remove(e, count, [&](auto slot, auto last) {
        ids[slot] = ids[last];
        id_lengths[slot] = id_lengths[last];
      });
    }

  protected:
    /** @brief Replace @p e's id in place. @pre has(e). */
    void assign(EntityId e, std::string_view id) noexcept {
      assert(has(e));
      write(index.dense_index(e), id);
    }

    /** @brief Non-owning view of @p e's id.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return View valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view id_of(EntityId e) const noexcept {
      assert(has(e));
      const auto slot = index.dense_index(e);
      return {ids[slot].data(), id_lengths[slot]};
    }

  private:
    void write(std::uint32_t slot, std::string_view id) noexcept {
      const auto length = std::min(id.size(), k_max_id_len - 1);
      std::ranges::copy(id | std::views::take(length), ids[slot].begin());
      ids[slot][length] = '\0';
      id_lengths[slot] = length;
    }
  };

} // namespace corundum::entities
