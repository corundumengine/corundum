// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cassert>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/sparse_index.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <span>

namespace corundum::entities {

  /** @brief What happens when a clip plays its last frame. */
  enum class ClipEnd : std::uint8_t {
    Resume, ///< Return to the sheet it interrupted. Moving also ends it early.
    Hold,   ///< Stay on the last frame until the entity goes.
    Remove, ///< Remove the entity.
  };

  /** @brief A one-shot sheet to play over an entity's walking and idling. */
  struct Clip {
    ClipEnd end{ClipEnd::Resume};

    std::array<std::uint8_t, corundum::sprites::k_num_anim_ids> frame_counts{};

    float frame_duration{}; ///< Seconds per frame; must be positive.

    corundum::sprites::SpriteId sprite{corundum::sprites::k_null_sprite_id};
  };

  /** @brief SoA table of the entities playing a clip. A row exists from animation::play_clip until
   *  a Resume clip ends, or the entity goes. animation::update leaves these entities alone and
   *  animation::update_clips plays them. */
  struct ClipTable {
    static constexpr auto k_max = k_max_entities;

    SparseIndex<k_max> index;

    std::array<ClipEnd, k_max> end{};

    std::array<float, k_max> frame_duration{};

    std::array<bool, k_max> is_finished{}; ///< A Hold or Remove clip has played its last frame.

    /// The sheet and frame counts to return to.
    std::array<corundum::sprites::SpriteId, k_max> resume_sprite{};

    std::array<std::array<std::uint8_t, corundum::sprites::k_num_anim_ids>, k_max> resume_counts{};

    std::uint32_t count = 0;

    [[nodiscard]] bool has(EntityId e) const noexcept { return index.has(e); }

    /** @brief Whether @p e's clip has played its last frame. @pre has(e). */
    [[nodiscard]] bool has_finished(EntityId e) const noexcept {
      assert(has(e));
      return is_finished[index.dense_index(e)];
    }

    /** @brief How @p e's clip ends. @pre has(e). */
    [[nodiscard]] ClipEnd end_of(EntityId e) const noexcept {
      assert(has(e));
      return end[index.dense_index(e)];
    }

    /** @brief Seconds per frame of @p e's clip. @pre has(e). */
    [[nodiscard]] float frame_duration_of(EntityId e) const noexcept {
      assert(has(e));
      return frame_duration[index.dense_index(e)];
    }

    /** @brief The sheet @p e returns to. @pre has(e). */
    [[nodiscard]] corundum::sprites::SpriteId resume_sprite_of(EntityId e) const noexcept {
      assert(has(e));
      return resume_sprite[index.dense_index(e)];
    }

    /** @brief The frame counts of the sheet @p e returns to. @pre has(e). */
    [[nodiscard]] const std::array<std::uint8_t, corundum::sprites::k_num_anim_ids> &
    resume_counts_of(EntityId e) const noexcept {
      assert(has(e));
      return resume_counts[index.dense_index(e)];
    }

    /** @brief Add a row for @p e. @pre has(e) must be false. */
    void insert(EntityId e, const Clip &clip, corundum::sprites::SpriteId sprite_to_resume,
                const std::array<std::uint8_t, corundum::sprites::k_num_anim_ids> &counts_to_resume) noexcept {
      index.insert(e, count, [&](auto slot) {
        end[slot] = clip.end;
        frame_duration[slot] = clip.frame_duration;
        is_finished[slot] = false;
        resume_sprite[slot] = sprite_to_resume;
        resume_counts[slot] = counts_to_resume;
      });
    }

    /** @brief Start @p clip in place of the one @p e is playing, keeping the sheet to return to.
     *  @pre has(e). */
    void replace(EntityId e, const Clip &clip) noexcept {
      assert(has(e));
      const std::uint32_t slot = index.dense_index(e);
      end[slot] = clip.end;
      frame_duration[slot] = clip.frame_duration;
      is_finished[slot] = false;
    }

    /** @brief Record that @p e's clip has played its last frame. @pre has(e). */
    void mark_finished(EntityId e) noexcept {
      assert(has(e));
      is_finished[index.dense_index(e)] = true;
    }

    /** @brief Remove @p e's row via swap-and-pop. @pre has(e). */
    void remove(EntityId e) noexcept {
      index.remove(e, count, [&](auto slot, auto last) {
        end[slot] = end[last];
        frame_duration[slot] = frame_duration[last];
        is_finished[slot] = is_finished[last];
        resume_sprite[slot] = resume_sprite[last];
        resume_counts[slot] = resume_counts[last];
      });
    }

    [[nodiscard]] auto active_span(this auto &self) noexcept { return std::span(self.end).first(self.count); }

    [[nodiscard]] auto active_entities(this auto &self) noexcept { return self.index.active_entities(self.count); }
  };

  static_assert(GameTable<ClipTable>);

} // namespace corundum::entities
