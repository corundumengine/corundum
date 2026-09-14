// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <cstdint>
#include <span>

namespace corundum::entities {

  /** @brief SoA table for Position and Velocity — always spawned together.
   *
   * All positions are in tile-grid fractional coordinates (col, row).
   * Velocities are in tiles per second (dc, dr).
   * Hot path (every frame): col, row, dc, dr — read and written by physics, input, and
   * animation systems.
   *
   * Uses a sparse–dense mapping: `index.sparse[entity_id]` → dense row index. Removal is
   * O(1) via swap-and-pop; the dense arrays are always contiguous.
   */
  struct TransformTable {
    static constexpr auto k_max = k_max_entities;

    // ── Hot: accessed every frame (SoA). Front-loaded so cold sparse/entities data
    // below never shares a cache line with — or sits immediately before — hot data. ──
    alignas(k_cache_line) std::array<float, k_max> col{};

    alignas(k_cache_line) std::array<float, k_max> row{};

    alignas(k_cache_line) std::array<float, k_max> dc{};

    alignas(k_cache_line) std::array<float, k_max> dr{};

    // ── Sparse index: EntityId → dense row ─────────────────────────
    SparseIndex<k_max> index;

    std::uint32_t count = 0;

    /** @brief Contiguous span over the tile columns of all live entities. */
    [[nodiscard]] auto active_cols(this auto &self) noexcept {
      return std::span(self.col).first(self.count);
    }

    /** @brief Contiguous span over the tile rows of all live entities. */
    [[nodiscard]] auto active_rows(this auto &self) noexcept {
      return std::span(self.row).first(self.count);
    }

    /** @brief Contiguous span over the tile-column velocities of all live entities. */
    [[nodiscard]] auto active_dcs(this auto &self) noexcept {
      return std::span(self.dc).first(self.count);
    }

    /** @brief Contiguous span over the tile-row velocities of all live entities. */
    [[nodiscard]] auto active_drs(this auto &self) noexcept {
      return std::span(self.dr).first(self.count);
    }

    /** @brief Contiguous span over col (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.col).first(self.count);
    }

    /** @brief Contiguous span of the EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return self.index.active_entities(self.count);
    }

    /** @brief True if @p e has a row in this table.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return index.has(e);
    }

    /** @brief Add a transform row for @p e.
     *  @param[in] e       Entity to add (must not already be present).
     *  @param[in] pos_col Initial tile column (fractional).
     *  @param[in] pos_row Initial tile row (fractional).
     *  @param[in] vel_col Initial column velocity in tiles per second.
     *  @param[in] vel_row Initial row velocity in tiles per second.
     *  @pre has(e) must be false.
     */
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    // pos_col/pos_row are the spawn position and vel_col/vel_row the spawn velocity — two
    // naturally paired (col,row) groups. A struct would add ceremony without preventing a
    // same-group swap.
    void insert(EntityId e, float pos_col, float pos_row, float vel_col, float vel_row) noexcept {
      index.insert(e, count, [&](auto slot) {
        col[slot] = pos_col;
        row[slot] = pos_row;
        dc[slot] = vel_col;
        dr[slot] = vel_row;
      });
    }

    // NOLINTEND(bugprone-easily-swappable-parameters)

    /** @brief Remove @p e's transform row via swap-and-pop.
     *  @param[in] e Entity to remove.
     *  @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      index.remove(e, count, [&](auto slot, auto last) {
        col[slot] = col[last];
        row[slot] = row[last];
        dc[slot] = dc[last];
        dr[slot] = dr[last];
      });
    }

    /** @brief Tile column of @p e. @pre has(e). */
    [[nodiscard]] float pos_col(EntityId e) const noexcept {
      assert(has(e));
      return col[index.dense_index(e)];
    }

    /** @brief Tile row of @p e. @pre has(e). */
    [[nodiscard]] float pos_row(EntityId e) const noexcept {
      assert(has(e));
      return row[index.dense_index(e)];
    }

    /** @brief Mutable tile column of @p e. @pre has(e). */
    [[nodiscard]] float &pos_col(EntityId e) noexcept {
      assert(has(e));
      return col[index.dense_index(e)];
    }

    /** @brief Mutable tile row of @p e. @pre has(e). */
    [[nodiscard]] float &pos_row(EntityId e) noexcept {
      assert(has(e));
      return row[index.dense_index(e)];
    }

    /** @brief Dense row index for @p e; use for direct SoA array subscript on hot paths.
     *  @pre has(e) must be true.
     *  @return Index into col/row/dc/dr arrays.
     */
    [[nodiscard]] std::uint32_t dense_index(EntityId e) const noexcept {
      return index.dense_index(e);
    }
  };

  static_assert(GameTable<TransformTable>);

} // namespace corundum::entities
