#pragma once
#include <array>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::component {

  /** @brief Table for the BoundingBox collision component.
   *
   * `col_span` and `row_span` are always read together (they form one collision rect), so
   * they stay as an AoS `Rect` inside the table. Per DOD §4.3: fields accessed as a
   * unit may remain AoS. The array of rects is `alignas(16)` for cache alignment.
   */
  struct CollisionTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Collision data (always loaded as a unit). Front-loaded so the cold sparse/
    // entities data below never shares a cache line with — or sits immediately
    // before — hot data. ────────────────────────────────────────────
    struct Rect {
      float col_span = 0.f;
      float row_span = 0.f;
    };

    alignas(k_cache_line) std::array<Rect, k_max> rects{};

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    std::uint32_t count = 0;

    CollisionTable() noexcept {
      sparse.fill(k_invalid);
    }

    /** @brief Contiguous span over collision rects for all live entities. */
    [[nodiscard]] auto active_rects(this auto &self) noexcept {
      return std::span(self.rects).first(self.count);
    }

    /** @brief Span over rects (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.rects).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has a collision rect. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add a collision rect for @p e.
     *  @param[in] e         Entity (must not already be present).
     *  @param[in] col_span  Horizontal collision extent in tile columns.
     *  @param[in] row_span  Vertical collision extent in tile rows.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float col_span, float row_span) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      rects[slot] = {col_span, row_span};
      ++count;
    }

    /** @brief Remove @p e's collision rect via swap-and-pop.
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
        rects[slot] = rects[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Const collision rect for @p e. @pre has(e). */
    [[nodiscard]] const Rect &get_rect(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable collision rect for @p e. @pre has(e). */
    [[nodiscard]] Rect &get_rect(EntityId e) noexcept {
      return rects[sparse[std::to_underlying(e)]];
    }

    /** @brief col_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float col_span(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]].col_span;
    }

    /** @brief row_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float row_span(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]].row_span;
    }
  };

  static_assert(GameTable<CollisionTable>);

} // namespace corundum::gameplay::component
