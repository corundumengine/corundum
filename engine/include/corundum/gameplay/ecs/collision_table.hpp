#pragma once
#include <array>
#include <corundum/gameplay/ecs/entity.hpp>
#include <corundum/gameplay/ecs/table_concepts.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::ecs {

  /** @brief Table for the BoundingBox collision component.
   *
   * `w`, `h`, and `yo` are always read together (they form one collision rect), so
   * they stay as an AoS `Rect` inside the table. Per DOD §4.3: fields accessed as a
   * unit may remain AoS. The array of rects is `alignas(16)` for cache alignment.
   */
  struct CollisionTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── Collision data (always loaded as a unit) ───────────────────
    struct Rect {
      float w = 0.f;
      float h = 0.f;
      float yo = 0.f;
    };

    alignas(16) std::array<Rect, k_max> rects{};

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
     *  @param[in] e  Entity (must not already be present).
     *  @param[in] w  Rect width in world pixels.
     *  @param[in] h  Rect height in world pixels.
     *  @param[in] yo Vertical offset from entity origin to top of rect.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, float w, float h, float yo) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      rects[slot] = {w, h, yo};
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

    /** @brief Width of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float w(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]].w;
    }

    /** @brief Height of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float h(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]].h;
    }

    /** @brief Vertical offset of @p e's collision rect. @pre has(e). */
    [[nodiscard]] float yo(EntityId e) const noexcept {
      return rects[sparse[std::to_underlying(e)]].yo;
    }
  };

  static_assert(GameTable<CollisionTable>);

} // namespace corundum::gameplay::ecs
