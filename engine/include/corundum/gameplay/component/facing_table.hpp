#pragma once
#include <array>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <span>

namespace corundum::gameplay::component {

  /** @brief SoA table for the Facing component.
   *
   * Only entities with directional behaviour (player, some NPCs) have an entry.
   * Facing is updated by the animation system when the entity moves.
   */
  struct FacingTable {
    static constexpr auto k_max = k_max_entities;

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<k_max> idx;

    // ── SoA field ──────────────────────────────────────────────────
    std::array<FacingDir, k_max> dir{};

    std::uint32_t count = 0;

    /** @brief Contiguous span over facing directions for all live entities. */
    [[nodiscard]] auto active_dir(this auto &self) noexcept {
      return std::span(self.dir).first(self.count);
    }

    /** @brief Span over dir (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.dir).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has a facing entry. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add a facing row for @p e.
     *  @param[in] e Entity (must not already be present).
     *  @param[in] d Initial facing direction.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, FacingDir d) noexcept {
      idx.insert(e, count, [&](auto slot) { dir[slot] = d; });
    }

    /** @brief Remove @p e's facing row via swap-and-pop.
     *  @param[in] e Entity to remove. @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) { dir[slot] = dir[last]; });
    }

    /** @brief Mutable facing direction reference for @p e. @pre has(e). */
    [[nodiscard]] FacingDir &dir_ref(EntityId e) noexcept {
      assert(has(e));
      return dir[idx.dense_idx(e)];
    }

    /** @brief Facing direction of @p e. @pre has(e). */
    [[nodiscard]] FacingDir dir_of(EntityId e) const noexcept {
      assert(has(e));
      return dir[idx.dense_idx(e)];
    }
  };

  static_assert(GameTable<FacingTable>);

} // namespace corundum::gameplay::component
