#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <string_view>

namespace corundum::gameplay::component {

  /** @brief SoA table for the DialogueRef component.
   *
   * Only NPCs that can initiate dialogue appear here. `graph_id` is stored as a
   * fixed-size `char[k_max_id_len]` buffer so the table remains trivially copyable
   * (required by the GameTable concept).
   */
  struct DialogueTable {
    static constexpr auto k_max = k_max_entities;
    static constexpr std::size_t k_max_id_len = 128;

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<k_max> idx;

    // ── Graph identifier (cold — read only on dialogue trigger) ────
    std::array<std::array<char, k_max_id_len>, k_max> graph_id{};
    std::array<std::size_t, k_max> graph_id_len{};

    std::uint32_t count = 0;

    /** @brief Span over graph_id buffers (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.graph_id).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has a dialogue ref. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add a dialogue ref for @p e.
     *  @param[in] e  Entity (must not already be present).
     *  @param[in] id Dialogue graph identifier string (truncated to k_max_id_len-1).
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, std::string_view id) noexcept {
      idx.insert(e, count, [&](auto slot) { set_id(slot, id); });
    }

    /** @brief Remove @p e's dialogue ref via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) {
        graph_id[slot] = graph_id[last];
        graph_id_len[slot] = graph_id_len[last];
      });
    }

    /** @brief Set (or replace) the graph ID for @p e.
     *  @param[in] e  Entity to update. @pre has(e) must be true.
     *  @param[in] id New graph identifier (truncated to k_max_id_len-1 if longer).
     */
    void set_graph_id(EntityId e, std::string_view id) noexcept {
      assert(has(e));
      set_id(idx.dense_idx(e), id);
    }

    /** @brief Graph ID string for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return Non-owning string_view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get_graph_id(EntityId e) const noexcept {
      assert(has(e));
      const auto slot = idx.dense_idx(e);
      return std::string_view(graph_id[slot].data(), graph_id_len[slot]);
    }

  private:
    void set_id(std::uint32_t slot, std::string_view id) noexcept {
      const auto len = std::min(id.size(), k_max_id_len - 1);
      std::copy_n(id.data(), len, graph_id[slot].data());
      graph_id[slot][len] = '\0';
      graph_id_len[slot] = len;
    }
  };

  static_assert(GameTable<DialogueTable>);

} // namespace corundum::gameplay::component
