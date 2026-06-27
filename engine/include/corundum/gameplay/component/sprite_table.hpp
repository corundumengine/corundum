#pragma once
#include <array>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/resources/sprite.hpp>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace corundum::gameplay::component {

  /** @brief SoA table for the Sprite rendering component.
   *
   * Every entity that appears on screen has an entry here. Absence from this table
   * means the entity is invisible. All fields are read every frame by the renderer.
   */
  struct SpriteTable {
    static constexpr auto k_max = k_max_entities;

    static constexpr std::uint32_t k_invalid = std::numeric_limits<std::uint32_t>::max();

    // ── Sparse index ───────────────────────────────────────────────
    std::array<std::uint32_t, k_max> sparse{};

    // ── Dense entity tracking ──────────────────────────────────────
    std::array<EntityId, k_max> entities{};

    // ── SoA fields (all hot — read every frame by the renderer) ────
    std::array<corundum::resources::SpriteId, k_max> sprite_id{};
    std::array<corundum::resources::AnimId, k_max> anim_id{};
    std::array<uint8_t, k_max> frame_index{};

    std::uint32_t count = 0;

    SpriteTable() noexcept {
      sparse.fill(k_invalid);
    }

    /** @brief Contiguous span of sprite IDs for all live entities. */
    [[nodiscard]] auto active_sprite_id(this auto &self) noexcept {
      return std::span(self.sprite_id).first(self.count);
    }

    /** @brief Contiguous span of animation IDs for all live entities. */
    [[nodiscard]] auto active_anim_id(this auto &self) noexcept {
      return std::span(self.anim_id).first(self.count);
    }

    /** @brief Contiguous span of frame indices for all live entities. */
    [[nodiscard]] auto active_frame_index(this auto &self) noexcept {
      return std::span(self.frame_index).first(self.count);
    }

    /** @brief Span over sprite_id (alias for GameTable concept compliance). */
    [[nodiscard]] auto active_span(this auto &self) noexcept {
      return std::span(self.sprite_id).first(self.count);
    }

    /** @brief Contiguous span of EntityIds in dense order. */
    [[nodiscard]] auto active_entities(this auto &self) noexcept {
      return std::span(self.entities).first(self.count);
    }

    /** @brief True if @p e has a sprite row.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      const auto idx = std::to_underlying(e);
      return idx < k_max && sparse[idx] != k_invalid;
    }

    /** @brief Add a sprite row for @p e.
     *  @param[in] e   Entity (must not already be present).
     *  @param[in] sid Sprite sheet index.
     *  @param[in] aid Initial animation clip.
     *  @param[in] fi  Initial frame index within the clip.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, corundum::resources::SpriteId sid, corundum::resources::AnimId aid, uint8_t fi) noexcept {
      const auto idx = std::to_underlying(e);
      const auto slot = count;
      sparse[idx] = slot;
      entities[slot] = e;
      sprite_id[slot] = sid;
      anim_id[slot] = aid;
      frame_index[slot] = fi;
      ++count;
    }

    /** @brief Remove @p e's sprite row via swap-and-pop.
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
        sprite_id[slot] = sprite_id[last];
        anim_id[slot] = anim_id[last];
        frame_index[slot] = frame_index[last];
      }
      sparse[idx] = k_invalid;
      --count;
    }

    /** @brief Dense row index for @p e; use to subscript SoA arrays directly.
     *  @pre has(e) must be true.
     */
    [[nodiscard]] std::uint32_t dense_idx(EntityId e) const noexcept {
      return sparse[std::to_underlying(e)];
    }

    /** @brief Mutable reference to the sprite ID for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::SpriteId &sprite_id_ref(EntityId e) noexcept {
      return sprite_id[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable reference to the animation ID for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::AnimId &anim_id_ref(EntityId e) noexcept {
      return anim_id[sparse[std::to_underlying(e)]];
    }

    /** @brief Mutable reference to the frame index for @p e. @pre has(e). */
    [[nodiscard]] uint8_t &frame_index_ref(EntityId e) noexcept {
      return frame_index[sparse[std::to_underlying(e)]];
    }
  };

  static_assert(GameTable<SpriteTable>);

} // namespace corundum::gameplay::component
