#pragma once
#include <array>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <span>

namespace corundum::gameplay::component {

  /** @brief Table for the BoundingBox collision component.
   *
   * `col_span` and `row_span` are always read together (they form one collision rect), so
   * they stay as an AoS `Rect` inside the table. Per DOD §4.3: fields accessed as a
   * unit may remain AoS. The array of rects is `alignas(16)` for cache alignment.
   */
  struct CollisionTable {
    static constexpr auto k_max = k_max_entities;

    // ── Collision data (always loaded as a unit). Front-loaded so the cold sparse/
    // entities data below never shares a cache line with — or sits immediately
    // before — hot data. ────────────────────────────────────────────
    struct Rect {
      float col_span = 0.f;
      float row_span = 0.f;
    };

    alignas(k_cache_line) std::array<Rect, k_max> rects{};

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<k_max> idx;

    std::uint32_t count = 0;

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
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has a collision rect. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add a collision rect for @p e.
     *  @param[in] e         Entity (must not already be present).
     *  @param[in] col_span  Horizontal collision extent in tile columns.
     *  @param[in] row_span  Vertical collision extent in tile rows.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float col_span, float row_span) noexcept {
      idx.insert(e, count, [&](auto slot) { rects[slot] = {col_span, row_span}; });
    }

    /** @brief Remove @p e's collision rect via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) { rects[slot] = rects[last]; });
    }

    /** @brief Const collision rect for @p e. @pre has(e). */
    [[nodiscard]] const Rect &get_rect(EntityId e) const noexcept {
      assert(has(e));
      return rects[idx.dense_idx(e)];
    }

    /** @brief Mutable collision rect for @p e. @pre has(e). */
    [[nodiscard]] Rect &get_rect(EntityId e) noexcept {
      assert(has(e));
      return rects[idx.dense_idx(e)];
    }

    /** @brief col_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float col_span(EntityId e) const noexcept {
      assert(has(e));
      return rects[idx.dense_idx(e)].col_span;
    }

    /** @brief row_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float row_span(EntityId e) const noexcept {
      assert(has(e));
      return rects[idx.dense_idx(e)].row_span;
    }
  };

  static_assert(GameTable<CollisionTable>);

} // namespace corundum::gameplay::component
