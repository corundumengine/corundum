// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <cstdint>
#include <span>

namespace corundum::entities {

  /** @brief Axis-aligned box in tile-grid units: top-left corner plus extent. */
  struct GridBox {
    float col = 0.f;

    float row = 0.f;

    float col_span = 0.f;

    float row_span = 0.f;
  };

  /** @brief Offset from an entity's tile-grid position to its sprite's feet anchor.
   *
   * Positions index tiles — an integer coordinate means "standing on that tile" — while
   * sprites are drawn at that tile's centre (tile_to_world_center), half a tile in on both
   * axes. Anything deriving a world-space box from a position has to anchor on the same
   * point, or the box sits half a tile off the character it belongs to.
   */
  inline constexpr float k_entity_anchor_offset = 0.5f;

  /** @brief The collision footprint of the entity standing on tile (@p col, @p row).
   *
   * The single definition of the CollisionTable footprint convention below: a box centred
   * on the sprite's feet anchor — (col, row) offset by k_entity_anchor_offset — so it covers
   * the tile the entity stands on.
   *
   * Anchoring the box's *bottom edge* on the feet instead pushes the whole box a row_span
   * north, into the tile above. That is what made a portal one row away fire the moment the
   * entity stood next to it, and it is how the pathfinder came to treat the cell north of an
   * NPC as free and route movement straight into it — so new consumers belong here.
   */
  [[nodiscard]] constexpr GridBox footprint_of(float col, float row, float col_span, float row_span) noexcept {
    const float feet_col = col + k_entity_anchor_offset;
    const float feet_row = row + k_entity_anchor_offset;
    return {
        .col = feet_col - (col_span * 0.5f),
        .row = feet_row - (row_span * 0.5f),
        .col_span = col_span,
        .row_span = row_span,
    };
  }

  /** @brief The entity position @p box was built from — footprint_of's inverse.
   *
   * Paired with footprint_of so the anchor offset is written down once: a caller converting
   * a resolved box back to a position must use this rather than re-deriving the arithmetic,
   * which would silently drift out of step with k_entity_anchor_offset.
   */
  [[nodiscard]] constexpr Position position_of(const GridBox &box) noexcept {
    return {
        .col = box.col + (box.col_span * 0.5f) - k_entity_anchor_offset,
        .row = box.row + (box.row_span * 0.5f) - k_entity_anchor_offset,
    };
  }

  /** @brief Table for the axis-aligned collision footprint of an entity.
   *
   * Footprints are in tile-grid units and populated once at spawn, so collision never needs a
   * per-frame registry lookup. A footprint extends from (col - col_span/2, row - row_span/2)
   * to (col + col_span/2, row + row_span/2) — centred on the entity's feet, which are the
   * tile centre for the tile it stands on.
   *
   * `col_span` and `row_span` are always read together (they form one collision rect), so
   * they stay as an AoS `Rect` inside the table. Per DOD §4.3: fields accessed as a
   * unit may remain AoS. The array of rects is `alignas(k_cache_line)` for cache alignment.
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
    SparseIndex<k_max> index;

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
      return self.index.active_entities(self.count);
    }

    /** @brief True if @p e has a collision rect. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return index.has(e);
    }

    /** @brief Add a collision rect for @p e.
     *  @param[in] e         Entity (must not already be present).
     *  @param[in] col_span  Horizontal collision extent in tile columns.
     *  @param[in] row_span  Vertical collision extent in tile rows.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float col_span, float row_span) noexcept {
      index.insert(e, count, [&](auto slot) { rects[slot] = {col_span, row_span}; });
    }

    /** @brief Remove @p e's collision rect via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      index.remove(e, count, [&](auto slot, auto last) { rects[slot] = rects[last]; });
    }

    /** @brief Const collision rect for @p e. @pre has(e). */
    [[nodiscard]] const Rect &get_rect(EntityId e) const noexcept {
      assert(has(e));
      return rects[index.dense_index(e)];
    }

    /** @brief Mutable collision rect for @p e. @pre has(e). */
    [[nodiscard]] Rect &get_rect(EntityId e) noexcept {
      assert(has(e));
      return rects[index.dense_index(e)];
    }

    /** @brief col_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float col_span(EntityId e) const noexcept {
      assert(has(e));
      return rects[index.dense_index(e)].col_span;
    }

    /** @brief row_span of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float row_span(EntityId e) const noexcept {
      assert(has(e));
      return rects[index.dense_index(e)].row_span;
    }
  };

  static_assert(GameTable<CollisionTable>);

} // namespace corundum::entities
