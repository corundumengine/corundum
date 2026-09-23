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
#include <memory>
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
    /// Sign combinations (dy_sign, dx_sign) covered by each zone.
    constexpr std::size_t k_facing_sign_count = 4;
    /// Distinct dx_sign values, used to stride within one zone's sign block.
    constexpr std::size_t k_facing_dx_sign_count = 2;

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
     * indexes k_facing_table by (zone, sign_dy, sign_dx).
     */
    Direction facing_from_velocity(float abs_dx, float abs_dy, float vel_dx, float vel_dy) noexcept {
      std::size_t zone = 2;
      if (abs_dy > k_cardinal_dominance_ratio * abs_dx) {
        zone = 0;
      } else if (abs_dx > k_cardinal_dominance_ratio * abs_dy) {
        zone = 1;
      }
      const std::size_t dy_sign = vel_dy > 0.f ? 1 : 0;
      const std::size_t dx_sign = vel_dx > 0.f ? 1 : 0;
      return k_facing_table[(zone * k_facing_sign_count) + (dy_sign * k_facing_dx_sign_count) + dx_sign];
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
    float compute_speed_scale(bool moving, float vel_dx, float vel_dy, float reference_speed,
                              core::math::IsometricParams iso) noexcept {
      if (!moving || reference_speed <= 0.f || iso.half_tw <= 0.f || iso.half_th <= 0.f) [[unlikely]]
        return 1.f;
      const auto [svx, svy] = core::math::tile_to_screen_delta(vel_dx, vel_dy, iso);
      return std::hypot(svx, svy) / reference_speed;
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
      const float transition_frame_duration = motion_sprites.frame_duration_for(e, desired);
      if (transition_frame_duration > 0.f)
        animations.frame_duration_ref(e) = transition_frame_duration;
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
                            Direction moving_facing, float abs_dx, float abs_dy) noexcept {
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
      const bool row_dominant = moving ? (abs_dy > abs_dx) : row_dominant_facing(facing);
      const AnimId fallback = row_dominant ? k_cardinal_fallback_v[facing_index] : k_cardinal_fallback_h[facing_index];
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
      const float frames_elapsed = std::floor(animation_timer / frame_duration);
      const auto frame_advance = static_cast<uint32_t>(std::fmod(frames_elapsed, static_cast<float>(frame_count)));
      animation_timer = std::fmod(animation_timer, frame_duration);
      sprite_frame_index = static_cast<uint8_t>((sprite_frame_index + frame_advance) % frame_count);
    }

  } // namespace

  void update(SpriteTable &sprites, const TransformTable &transforms, AnimationTable &animations, FacingTable &facings,
              MotionSpriteTable &motion_sprites, core::math::IsometricParams iso, float reference_speed,
              float dt) noexcept {
    [[assume(animations.count <= AnimationTable::k_max)]];
    float *const timers = std::assume_aligned<16>(animations.timer.data());
    const float *const frame_durations = std::assume_aligned<16>(animations.frame_duration.data());
    for (uint32_t i = 0; i < animations.count; ++i) {
      const EntityId e = animations.index.entities[i];
      if (!sprites.has(e) || !transforms.has(e)) [[unlikely]]
        continue;

      const uint32_t transform_slot = transforms.dense_index(e);

      AnimId &sprite_anim_id = sprites.anim_id_ref(e);
      uint8_t &sprite_frame_index = sprites.frame_index_ref(e);
      float &animation_timer = timers[i];
      const float &entity_frame_duration = frame_durations[i];

      const float &vel_dx = transforms.dc[transform_slot];
      const float &vel_dy = transforms.dr[transform_slot];

      const bool moving = (vel_dx != 0.f || vel_dy != 0.f);
      const float abs_dx = std::abs(vel_dx);
      const float abs_dy = std::abs(vel_dy);

      const float speed_scale = compute_speed_scale(moving, vel_dx, vel_dy, reference_speed, iso);

      tick_motion_sprite(sprites, animations, motion_sprites, e, moving, dt);

      const Direction facing = moving ? facing_from_velocity(abs_dx, abs_dy, vel_dx, vel_dy) : Direction::South;

      if (moving && facings.has(e)) [[likely]]
        facings.dir_ref(e) = facing;

      const AnimId target = pick_target_anim(animations, facings, e, moving, facing, abs_dx, abs_dy);

      if (sprite_anim_id != target) [[unlikely]] {
        sprite_anim_id = target;
        sprite_frame_index = 0;
        animation_timer = 0.f;
      }

      const uint8_t frame_count = animations.frame_count(e, sprite_anim_id);
      advance_frame_timer(animation_timer, entity_frame_duration, frame_count, sprite_frame_index, dt, speed_scale);
    }
  }

} // namespace corundum::animation
