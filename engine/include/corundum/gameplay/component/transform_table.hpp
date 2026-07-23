#pragma once
#include <array>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <span>

namespace corundum::gameplay::component {

  /** @brief SoA table for Position and Velocity — always spawned together.
   *
   * All positions are in tile-grid fractional coordinates (col, row).
   * Velocities are in tiles per second (dc, dr).
   * Hot path (every frame): col, row, dc, dr — read and written by physics, input, and
   * animation systems. Debug labels live in the paired TransformNameTable to keep
   * this struct cache-clean.
   *
   * Uses a sparse–dense mapping: `sparse[entity_id]` → dense row index. Removal is
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
    SparseIndex<k_max> idx;

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
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has a row in this table.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add a transform row for @p e.
     *  @param[in] e   Entity to add (must not already be present).
     *  @param[in] pc  Initial tile column (fractional).
     *  @param[in] pr  Initial tile row (fractional).
     *  @param[in] vc  Initial column velocity in tiles per second.
     *  @param[in] vr  Initial row velocity in tiles per second.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float pc, float pr, float vc, float vr) noexcept {
      idx.insert(e, count, [&](auto slot) {
        col[slot] = pc;
        row[slot] = pr;
        dc[slot] = vc;
        dr[slot] = vr;
      });
    }

    /** @brief Remove @p e's transform row via swap-and-pop.
     *  @param[in] e Entity to remove.
     *  @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) {
        col[slot] = col[last];
        row[slot] = row[last];
        dc[slot] = dc[last];
        dr[slot] = dr[last];
      });
    }

    /** @brief Tile column of @p e. @pre has(e). */
    [[nodiscard]] float pos_col(EntityId e) const noexcept {
      assert(has(e));
      return col[idx.dense_idx(e)];
    }

    /** @brief Tile row of @p e. @pre has(e). */
    [[nodiscard]] float pos_row(EntityId e) const noexcept {
      assert(has(e));
      return row[idx.dense_idx(e)];
    }

    /** @brief Mutable tile column of @p e. @pre has(e). */
    [[nodiscard]] float &pos_col(EntityId e) noexcept {
      assert(has(e));
      return col[idx.dense_idx(e)];
    }

    /** @brief Mutable tile row of @p e. @pre has(e). */
    [[nodiscard]] float &pos_row(EntityId e) noexcept {
      assert(has(e));
      return row[idx.dense_idx(e)];
    }

    /** @brief Dense row index for @p e; use for direct SoA array subscript on hot paths.
     *  @pre has(e) must be true.
     *  @return Index into col/row/dc/dr arrays.
     */
    [[nodiscard]] std::uint32_t dense_idx(EntityId e) const noexcept {
      return idx.dense_idx(e);
    }
  };

  static_assert(GameTable<TransformTable>);

} // namespace corundum::gameplay::component
