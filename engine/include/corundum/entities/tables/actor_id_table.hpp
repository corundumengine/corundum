// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <array>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <ranges>
#include <string_view>

namespace corundum::entities {

  /** @brief SoA table for the stable authoring id of an actor.
   *
   * One row per spawned entity that carries a non-empty `Actor::id`. The id is
   * stored as a fixed-size `char[k_max_id_len]` buffer so the table remains
   * trivially copyable (required by the GameTable concept). Quests and saves
   * reference NPCs by this id — it survives chunk respawns that change the
   * entity handle.
   */
  struct ActorIdTable {
    static constexpr auto k_max = k_max_entities;
    static constexpr std::size_t k_max_id_len = 128;

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<k_max> idx;

    // ── Actor id (cold — read only for quest/save references) ───────
    std::array<std::array<char, k_max_id_len>, k_max> id{};
    std::array<std::size_t, k_max> id_len{};

    std::uint32_t count = 0;

    /** @brief Span over id buffers (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.id).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has an actor id. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add an actor id for @p e.
     *  @param[in] e  Entity (must not already be present).
     *  @param[in] actor_id Stable authoring id string (truncated to k_max_id_len-1).
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, std::string_view actor_id) noexcept {
      idx.insert(e, count, [&](auto slot) { set_id(slot, actor_id); });
    }

    /** @brief Remove @p e's actor id via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) {
        id[slot] = id[last];
        id_len[slot] = id_len[last];
      });
    }

    /** @brief Actor id string for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return Non-owning string_view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get_actor_id(EntityId e) const noexcept {
      assert(has(e));
      const auto slot = idx.dense_idx(e);
      return {id[slot].data(), id_len[slot]};
    }

  private:
    void set_id(std::uint32_t slot, std::string_view actor_id) noexcept {
      const auto len = std::min(actor_id.size(), k_max_id_len - 1);
      std::ranges::copy(actor_id | std::views::take(len), id[slot].begin());
      id[slot][len] = '\0';
      id_len[slot] = len;
    }
  };

  static_assert(GameTable<ActorIdTable>);

} // namespace corundum::entities