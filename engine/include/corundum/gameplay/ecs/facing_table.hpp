#pragma once
#include <algorithm>
#include <array>
#include <corundum/gameplay/ecs/components.hpp>
#include <corundum/gameplay/ecs/entity.hpp>
#include <corundum/gameplay/ecs/table_concepts.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::ecs {

  /** @brief SoA table for the Facing component.
   *
   * Only entities with directional behaviour (player, some NPCs) have an entry.
   * Facing is updated by the animation system when the entity moves.
   */
  struct FacingTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── SoA field ──────────────────────────────────────────────────
    std::array<FacingDir, k_max> dir{};

    std::uint32_t count = 0;

    FacingTable() noexcept {
      sparse.fill(k_invalid);
      std::fill(dir.begin(), dir.end(), FacingDir::South);
    }

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
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has a facing entry. @param[in] e Entity to query. */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add a facing row for @p e.
     *  @param[in] e Entity (must not already be present).
     *  @param[in] d Initial facing direction.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, FacingDir d) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      dir[slot] = d;
      ++count;
    }

    /** @brief Remove @p e's facing row via swap-and-pop.
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
        dir[slot] = dir[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Mutable facing direction reference for @p e. @pre has(e). */
    [[nodiscard]] FacingDir &dir_ref(EntityId e) noexcept {
      return dir[sparse[std::to_underlying(e)]];
    }

    /** @brief Facing direction of @p e. @pre has(e). */
    [[nodiscard]] FacingDir dir_of(EntityId e) const noexcept {
      return dir[sparse[std::to_underlying(e)]];
    }
  };

  static_assert(GameTable<FacingTable>);

} // namespace corundum::gameplay::ecs
