#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/ecs/entity.hpp>
#include <corundum/gameplay/ecs/table_concepts.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace corundum::gameplay::ecs {

  /** @brief SoA table for the DialogueRef component.
   *
   * Only NPCs that can initiate dialogue appear here. `graph_id` is stored as a
   * fixed-size `char[k_max_id_len]` buffer so the table remains trivially copyable
   * (required by the GameTable concept).
   */
  struct DialogueTable {
    static constexpr auto k_max = k_max_entities;
    static constexpr std::size_t k_max_id_len = 128;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Graph identifier (cold — read only on dialogue trigger) ────
    std::array<std::array<char, k_max_id_len>, k_max> graph_id{};
    std::array<std::size_t, k_max> graph_id_len{};

    std::uint32_t count = 0;

    DialogueTable() noexcept {
      sparse.fill(k_invalid);
    }

    /** @brief Span over graph_id buffers (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.graph_id).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has a dialogue ref. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add a dialogue ref for @p e.
     *  @param[in] e  Entity (must not already be present).
     *  @param[in] id Dialogue graph identifier string (truncated to k_max_id_len-1).
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, std::string_view id) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      set_id(slot, id);
      ++count;
    }

    /** @brief Remove @p e's dialogue ref via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = sparse[idx];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[std::to_underlying(last_e)] = slot;
        entities[slot] = last_e;
        graph_id[slot] = graph_id[last];
        graph_id_len[slot] = graph_id_len[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Set (or replace) the graph ID for @p e.
     *  @param[in] e  Entity to update. @pre has(e) must be true.
     *  @param[in] id New graph identifier (truncated to k_max_id_len-1 if longer).
     */
    void set_graph_id(EntityId e, std::string_view id) noexcept {
      const auto slot = sparse[std::to_underlying(e)];
      set_id(slot, id);
    }

    /** @brief Graph ID string for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return Non-owning string_view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get_graph_id(EntityId e) const noexcept {
      const auto slot = sparse[std::to_underlying(e)];
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

} // namespace corundum::gameplay::ecs
