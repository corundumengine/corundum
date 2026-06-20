#pragma once
#include <array>
#include <corundum/gameplay/ecs/entity.hpp>
#include <corundum/gameplay/ecs/table_concepts.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::ecs {

  /** @brief SoA table for Position and Velocity — always spawned together.
   *
   * Hot path (every frame): x, y, dx, dy — read and written by physics, input, and
   * animation systems. Debug labels live in the paired TransformNameTable to keep
   * this struct cache-clean.
   *
   * Uses a sparse–dense mapping: `sparse[entity_id]` → dense row index. Removal is
   * O(1) via swap-and-pop; the dense arrays are always contiguous.
   */
  struct TransformTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index: EntityId → dense row ─────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Hot: accessed every frame (SoA) ────────────────────────────
    alignas(16) std::array<float, k_max> x{};
    alignas(16) std::array<float, k_max> y{};
    alignas(16) std::array<float, k_max> dx{};
    alignas(16) std::array<float, k_max> dy{};

    std::uint32_t count = 0;

    TransformTable() noexcept {
      sparse.fill(k_invalid);
    }

    /** @brief Contiguous span over the x positions of all live entities. */
    [[nodiscard]] auto active_x(this auto &self) noexcept {
      return std::span(self.x).first(self.count);
    }

    /** @brief Contiguous span over the y positions of all live entities. */
    [[nodiscard]] auto active_y(this auto &self) noexcept {
      return std::span(self.y).first(self.count);
    }

    /** @brief Contiguous span over the x velocities of all live entities. */
    [[nodiscard]] auto active_dx(this auto &self) noexcept {
      return std::span(self.dx).first(self.count);
    }

    /** @brief Contiguous span over the y velocities of all live entities. */
    [[nodiscard]] auto active_dy(this auto &self) noexcept {
      return std::span(self.dy).first(self.count);
    }

    /** @brief Contiguous span over x (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.x).first(self.count);
    }

    /** @brief Contiguous span of the EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has a row in this table.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add a transform row for @p e.
     *  @param[in] e   Entity to add (must not already be present).
     *  @param[in] px  Initial x position in world pixels.
     *  @param[in] py  Initial y position in world pixels.
     *  @param[in] vx  Initial x velocity in world pixels per second.
     *  @param[in] vy  Initial y velocity in world pixels per second.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float px, float py, float vx, float vy) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      x[slot] = px;
      y[slot] = py;
      dx[slot] = vx;
      dy[slot] = vy;
      ++count;
    }

    /** @brief Remove @p e's transform row via swap-and-pop.
     *  @param[in] e Entity to remove.
     *  @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = sparse[idx];
      const auto last = count - 1;
      if (slot != last) {
        const EntityId last_e = entities[last];
        sparse[std::to_underlying(last_e)] = slot;
        entities[slot] = last_e;
        x[slot] = x[last];
        y[slot] = y[last];
        dx[slot] = dx[last];
        dy[slot] = dy[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief X position of @p e. @pre has(e). */
    [[nodiscard]] float pos_x(EntityId e) const noexcept {
      return x[sparse[std::to_underlying(e)]];
    }

    /** @brief Y position of @p e. @pre has(e). */
    [[nodiscard]] float pos_y(EntityId e) const noexcept {
      return y[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable x position of @p e. @pre has(e). */
    [[nodiscard]] float &pos_x(EntityId e) noexcept {
      return x[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable y position of @p e. @pre has(e). */
    [[nodiscard]] float &pos_y(EntityId e) noexcept {
      return y[sparse[std::to_underlying(e)]];
    }

    /** @brief Dense row index for @p e; use for direct SoA array subscript on hot paths.
     *  @pre has(e) must be true.
     *  @return Index into x/y/dx/dy arrays.
     */
    [[nodiscard]] std::uint32_t dense_idx(EntityId e) const noexcept {
      return sparse[std::to_underlying(e)];
    }
  };

  static_assert(GameTable<TransformTable>);

} // namespace corundum::gameplay::ecs
