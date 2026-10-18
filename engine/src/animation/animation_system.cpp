// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <corundum/animation/animation_system.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/entity.hpp>

#include <corundum/core/direction.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace corundum::animation {

  namespace {
    using corundum::core::Direction;
    using corundum::entities::AnimationTable;
    using corundum::entities::EntityId;
    using corundum::entities::FacingTable;
    using corundum::entities::MotionSpriteTable;
    using corundum::entities::SpriteTable;
    using corundum::entities::TransformTable;
    using corundum::sprites::AnimId;
    using corundum::sprites::k_null_sprite_id;
    using corundum::sprites::SpriteId;
    using corundum::sprites::to_anim;

    /// Axis-dominance zones: 0 = row-dominant, 1 = col-dominant, 2 = diagonal.
    constexpr std::size_t k_facing_zone_count = 3;
    /// Sign combinations (dr_sign, dc_sign) covered by each zone.
    constexpr std::size_t k_facing_sign_count = 4;
    /// Distinct dc_sign values, used to stride within one zone's sign block.
    constexpr std::size_t k_facing_dc_sign_count = 2;

    /// Below this tile-space speed an entity counts as still. Physics writes an exact zero
    /// when idle, but a fractionally retained velocity must not read as motion.
    constexpr float k_motion_speed_epsilon = 1e-4f;

    constexpr std::array k_facing_table = {
        // zone 0 (row/vertical dominant: |dr| >> |dc|)
        // row axis (dr) maps to NE/SW on screen; entries here use Direction in screen-space terms.
        Direction::NorthEast, // dr<0, dc<0  (NE-ish on screen)
        Direction::NorthEast, // dr<0, dc>0  (NE-ish on screen)
        Direction::SouthWest, // dr>0, dc<0  (SW-ish on screen)
        Direction::SouthWest, // dr>0, dc>0  (SW-ish on screen)
        // zone 1 (col/horizontal dominant: |dc| >> |dr|)
        // col axis (dc) maps to NW/SE on screen.
        Direction::NorthWest, // dc<0, dr<0  (NW-ish on screen)
        Direction::SouthEast, // dc>0, dr<0  (SE-ish on screen)
        Direction::NorthWest, // dc<0, dr>0  (NW-ish on screen)
        Direction::SouthEast, // dc>0, dr>0  (SE-ish on screen)
        // zone 2 (diagonal: |dc| ≈ |dr|)
        // Pure screen-cardinal directions from combined tile axes.
        Direction::North, // dr<0, dc<0  (NW tile = up on screen)
        Direction::East,  // dr<0, dc>0  (NE tile = right on screen)
        Direction::West,  // dr>0, dc<0  (SW tile = left on screen)
        Direction::South, // dr>0, dc>0  (SE tile = down on screen)
    };

    // Fallbacks indexed by Direction value (AnimId's directional values mirror Direction).
    constexpr std::array k_cardinal_fallback_h = {
        AnimId::South, AnimId::North, AnimId::East, AnimId::West,
        AnimId::East,  AnimId::East,  AnimId::West, AnimId::West,
    };

    constexpr std::array k_cardinal_fallback_v = {
        AnimId::South, AnimId::North, AnimId::East,  AnimId::West,
        AnimId::North, AnimId::South, AnimId::South, AnimId::North,
    };

    /// Ratio above which the dominant velocity axis snaps fully cardinal (zone 0 or 1)
    /// instead of remaining diagonal (zone 2).
    constexpr float k_cardinal_dominance_ratio = 2.f;

    // Deduced extents (CTAD) so these checks catch a table that no longer covers the full
    // set — adding a Direction or a zone fails to compile instead of silently mis-indexing.
    static_assert(k_facing_table.size() == k_facing_zone_count * k_facing_sign_count,
                  "k_facing_table must cover every (zone, sign) combination");
    static_assert(k_cardinal_fallback_h.size() == core::k_num_directions,
                  "k_cardinal_fallback_h must cover every Direction value");
    static_assert(k_cardinal_fallback_v.size() == core::k_num_directions,
                  "k_cardinal_fallback_v must cover every Direction value");

    /** @brief Map a tile-space velocity to a screen-space facing direction.
     *
     * Splits the (|dr|, |dc|) plane into three zones by axis-dominance ratio, then
     * indexes k_facing_table by (zone, dr_sign, dc_sign).
     */
    Direction facing_from_velocity(float abs_dc, float abs_dr, float vel_dc, float vel_dr) noexcept {
      std::size_t zone{2};
      if (abs_dr > k_cardinal_dominance_ratio * abs_dc) {
        zone = 0;
      } else if (abs_dc > k_cardinal_dominance_ratio * abs_dr) {
        zone = 1;
      }
      const std::size_t dr_sign = vel_dr > 0.f ? 1 : 0;
      const std::size_t dc_sign = vel_dc > 0.f ? 1 : 0;
      return k_facing_table[(zone * k_facing_sign_count) + (dr_sign * k_facing_dc_sign_count) + dc_sign];
    }

    /** @brief Row/vertical dominance of a resolved facing without a velocity to measure.
     *
     * The row-dominant diagonals (NE/SW) fall back along the vertical cardinal axis and the
     * col-dominant diagonals (NW/SE) along the horizontal one; cardinals are unaffected
     * because either fallback table maps them to themselves.
     */
    constexpr bool row_dominant_facing(Direction facing) noexcept {
      return facing == Direction::NorthEast || facing == Direction::SouthWest;
    }

    /** @brief Animation playback speed scale for one frame.
     *
     * Idle (zero velocity) must NOT scale to 0 (would freeze the animation). Only
     * scale while actually moving, and guard reference_speed/iso being degenerate
     * (misconfigured) so this never divides by zero or propagates NaN.
     */
    float compute_speed_scale(bool moving, float vel_dc, float vel_dr, float reference_speed,
                              core::math::IsometricParams iso) noexcept {
      if (!moving || reference_speed <= 0.f || iso.half_tw <= 0.f || iso.half_th <= 0.f) [[unlikely]]
        return 1.f;
      const auto [screen_vel_x, screen_vel_y] = core::math::tile_to_screen_delta(vel_dc, vel_dr, iso);
      return std::hypot(screen_vel_x, screen_vel_y) / reference_speed;
    }

    /** @brief Playback duration for @p e's active sprite sheet.
     *
     * The AnimationTable value is the authored base rate; a non-zero override on the
     * active motion-sprite sheet replaces it. The base is never overwritten, so a sheet
     * with no override reverts to it.
     */
    float effective_frame_duration(const AnimationTable &animations, const SpriteTable &sprites,
                                   const MotionSpriteTable &motion_sprites, EntityId e,
                                   std::size_t animation_slot) noexcept {
      const float base_frame_duration = animations.frame_duration[animation_slot];
      if (!motion_sprites.has(e)) [[likely]]
        return base_frame_duration;
      const SpriteId active_sprite = sprites.sprite_id[sprites.dense_index(e)];
      const float sheet_frame_duration = motion_sprites.frame_duration_for(e, active_sprite);
      return sheet_frame_duration > 0.f ? sheet_frame_duration : base_frame_duration;
    }

    /** @brief Drive the walk/idle sprite-sheet transition for one entity.
     *
     * Updates the current sprite, frame counts, frame index, and timer when a transition
     * commits. No-op when @p e is not in @p motion_sprites or no transition is pending.
     */
    void tick_motion_sprite(SpriteTable &sprites, AnimationTable &animations, MotionSpriteTable &motion_sprites,
                            EntityId e, bool moving, float dt) noexcept {
      if (!motion_sprites.has(e)) [[likely]]
        return;

      const SpriteId desired = moving ? motion_sprites.walk_sprite(e) : motion_sprites.idle_sprite(e);
      SpriteId &current_sprite_id = sprites.sprite_id_ref(e);
      if (current_sprite_id == desired) {
        if (motion_sprites.pending_sprite(e) != k_null_sprite_id)
          motion_sprites.cancel_transition(e);
        return;
      }

      if (motion_sprites.pending_sprite(e) != desired)
        motion_sprites.set_pending(e, desired);

      const float elapsed = motion_sprites.tick_transition(e, dt);
      if (elapsed < motion_sprites.delay_for(e, desired)) [[likely]]
        return;

      current_sprite_id = desired;
      animations.set_frame_counts(e,
                                  moving ? motion_sprites.walk_frame_counts(e) : motion_sprites.idle_frame_counts(e));
      sprites.frame_index_ref(e) = 0;
      animations.timer_ref(e) = 0.f;
      motion_sprites.cancel_transition(e);
    }

    /** @brief Pick the animation clip to play this tick.
     *
     * While moving, uses the current velocity-derived facing; when idle, preserves
     * the entity's last recorded facing (or South if unknown). Falls back to a
     * cardinal clip when the directional clip is absent, then to Default.
     */
    AnimId pick_target_anim(const AnimationTable &animations, const FacingTable &facings, EntityId e, bool moving,
                            Direction moving_facing) noexcept {
      Direction facing = Direction::South;
      if (moving) {
        facing = moving_facing;
      } else if (facings.has(e)) {
        facing = facings.dir_of(e);
      }
      if (facing == Direction::Count) [[unlikely]]
        facing = Direction::South;

      const AnimId direction_anim = to_anim(facing);
      if (animations.frame_count(e, direction_anim) > 0)
        return direction_anim;
      const auto facing_index = std::to_underlying(facing);
      // Both fallback tables agree on the four cardinals; for a diagonal the direction
      // itself fixes row-vs-col dominance, whether the facing is fresh or last stored.
      const AnimId fallback =
          row_dominant_facing(facing) ? k_cardinal_fallback_v[facing_index] : k_cardinal_fallback_h[facing_index];
      return animations.frame_count(e, fallback) > 0 ? fallback : AnimId::Default;
    }

    /** @brief Advance the playback timer and wrap the frame index when it crosses a frame.
     *
     * No-op when the clip has 0 or 1 frame (no animation possible) or the frame duration is
     * non-positive.
     */
    void advance_frame_timer(float &animation_timer, float frame_duration, uint8_t frame_count,
                             uint8_t &sprite_frame_index, float dt, float speed_scale) noexcept {
      if (frame_count <= 1 || frame_duration <= 0.f) [[unlikely]]
        return;
      animation_timer += dt * speed_scale;
      if (animation_timer < frame_duration) [[likely]]
        return;
      // Advance every whole frame that elapsed this tick, wrapping the count modulo the
      // clip length so playback keeps the authored rate; fmod keeps the residual within
      // one frame without a float-to-integer conversion that could exceed uint32 range.
      const float frames_elapsed = animation_timer / frame_duration;
      const auto frame_advance = static_cast<uint32_t>(std::fmod(frames_elapsed, static_cast<float>(frame_count)));
      animation_timer = std::fmod(animation_timer, frame_duration);
      sprite_frame_index = static_cast<uint8_t>((sprite_frame_index + frame_advance) % frame_count);
    }

  } // namespace

  void update(SpriteTable &sprites, const TransformTable &transforms, AnimationTable &animations, FacingTable &facings,
              MotionSpriteTable &motion_sprites, core::math::IsometricParams iso, float reference_speed,
              float dt) noexcept {
    for (uint32_t i = 0; i < animations.count; ++i) {
      const EntityId e = animations.index.entities[i];
      if (!sprites.has(e) || !transforms.has(e)) [[unlikely]]
        continue;

      const uint32_t transform_slot = transforms.dense_index(e);

      AnimId &sprite_anim_id = sprites.anim_id_ref(e);
      uint8_t &sprite_frame_index = sprites.frame_index_ref(e);
      float &animation_timer = animations.timer[i];

      const float vel_dc = transforms.dc[transform_slot];
      const float vel_dr = transforms.dr[transform_slot];

      const float abs_dc = std::abs(vel_dc);
      const float abs_dr = std::abs(vel_dr);
      const bool moving = abs_dc > k_motion_speed_epsilon || abs_dr > k_motion_speed_epsilon;

      const float speed_scale = compute_speed_scale(moving, vel_dc, vel_dr, reference_speed, iso);

      tick_motion_sprite(sprites, animations, motion_sprites, e, moving, dt);

      const Direction facing = moving ? facing_from_velocity(abs_dc, abs_dr, vel_dc, vel_dr) : Direction::South;

      if (moving && facings.has(e)) [[likely]]
        facings.dir_ref(e) = facing;

      const AnimId target = pick_target_anim(animations, facings, e, moving, facing);

      if (sprite_anim_id != target) [[unlikely]] {
        sprite_anim_id = target;
        sprite_frame_index = 0;
        animation_timer = 0.f;
      }

      // Read after tick_motion_sprite: a committed transition may have replaced the
      // active sheet, and with it the duration that plays back this frame.
      const float frame_duration = effective_frame_duration(animations, sprites, motion_sprites, e, i);
      const uint8_t frame_count = animations.frame_count(e, sprite_anim_id);
      advance_frame_timer(animation_timer, frame_duration, frame_count, sprite_frame_index, dt, speed_scale);
    }
  }

} // namespace corundum::animation
