#pragma once
#include <array>
#include <corundum/gameplay/component/sparse_index.hpp>
#include <corundum/gameplay/component/table_concepts.hpp>
#include <corundum/resources/sprite.hpp>
#include <span>

namespace corundum::gameplay::component {

  /** @brief SoA table for the Sprite rendering component.
   *
   * Every entity that appears on screen has an entry here. Absence from this table
   * means the entity is invisible. All fields are read every frame by the renderer.
   */
  struct SpriteTable {
    static constexpr auto k_max = k_max_entities;

    // ── Sparse index ───────────────────────────────────────────────
    SparseIndex<k_max> idx;

    // ── SoA fields (all hot — read every frame by the renderer) ────
    std::array<corundum::resources::SpriteId, k_max> sprite_id{};
    std::array<corundum::resources::AnimId, k_max> anim_id{};
    std::array<uint8_t, k_max> frame_index{};

    std::uint32_t count = 0;

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
      return self.idx.active_entities(self.count);
    }

    /** @brief True if @p e has a sprite row.
     *  @param[in] e Entity to query.
     */
    [[nodiscard]] bool has(EntityId e) const noexcept {
      return idx.has(e);
    }

    /** @brief Add a sprite row for @p e.
     *  @param[in] e   Entity (must not already be present).
     *  @param[in] sid Sprite sheet index.
     *  @param[in] aid Initial animation clip.
     *  @param[in] fi  Initial frame index within the clip.
     *  @pre has(e) must be false.
     */
    void insert(EntityId e, corundum::resources::SpriteId sid, corundum::resources::AnimId aid, uint8_t fi) noexcept {
      idx.insert(e, count, [&](auto slot) {
        sprite_id[slot] = sid;
        anim_id[slot] = aid;
        frame_index[slot] = fi;
      });
    }

    /** @brief Remove @p e's sprite row via swap-and-pop.
     *  @param[in] e Entity to remove.
     *  @pre has(e) must be true.
     */
    void remove(EntityId e) noexcept {
      idx.remove(e, count, [&](auto slot, auto last) {
        sprite_id[slot] = sprite_id[last];
        anim_id[slot] = anim_id[last];
        frame_index[slot] = frame_index[last];
      });
    }

    /** @brief Dense row index for @p e; use to subscript SoA arrays directly.
     *  @pre has(e) must be true.
     */
    [[nodiscard]] std::uint32_t dense_idx(EntityId e) const noexcept {
      return idx.dense_idx(e);
    }

    /** @brief Mutable reference to the sprite ID for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::SpriteId &sprite_id_ref(EntityId e) noexcept {
      assert(has(e));
      return sprite_id[idx.dense_idx(e)];
    }

    /** @brief Mutable reference to the animation ID for @p e. @pre has(e). */
    [[nodiscard]] corundum::resources::AnimId &anim_id_ref(EntityId e) noexcept {
      assert(has(e));
      return anim_id[idx.dense_idx(e)];
    }

    /** @brief Mutable reference to the frame index for @p e. @pre has(e). */
    [[nodiscard]] uint8_t &frame_index_ref(EntityId e) noexcept {
      assert(has(e));
      return frame_index[idx.dense_idx(e)];
    }
  };

  static_assert(GameTable<SpriteTable>);

} // namespace corundum::gameplay::component
