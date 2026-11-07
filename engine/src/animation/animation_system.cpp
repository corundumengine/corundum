// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <corundum/animation/animation_system.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/entity.hpp>

#include <corundum/core/direction.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace corundum::animation {

  namespace {
    using corundum::core::Direction;
    using corundum::entities::AnimationTable;
    using corundum::entities::Clip;
    using corundum::entities::ClipEnd;
    using corundum::entities::ClipTable;
    using corundum::entities::EntityId;
    using corundum::entities::FacingTable;
    using corundum::entities::MotionSpriteTable;
    using corundum::entities::SpriteTable;
    using corundum::entities::TransformTable;
    using corundum::sprites::AnimId;
    using corundum::sprites::k_null_sprite_id;
    using corundum::sprites::SpriteId;
    using corundum::sprites::to_anim;

    /// Below this tile-space speed an entity counts as still. Physics writes an exact zero
    /// when idle, but a fractionally retained velocity must not read as motion.
    constexpr float k_motion_speed_epsilon = 1e-4f;

    // Fallbacks indexed by Direction value (AnimId's directional values mirror Direction).
    constexpr std::array k_cardinal_fallback_h = {
        AnimId::South, AnimId::North, AnimId::East, AnimId::West,
        AnimId::East,  AnimId::East,  AnimId::West, AnimId::West,
    };

    constexpr std::array k_cardinal_fallback_v = {
        AnimId::South, AnimId::North, AnimId::East,  AnimId::West,
        AnimId::North, AnimId::South, AnimId::South, AnimId::North,
    };

    // Deduced extents (CTAD) so these checks catch a table that no longer covers the full
    // set — adding a Direction fails to compile instead of silently mis-indexing.
    static_assert(k_cardinal_fallback_h.size() == core::k_num_directions,
                  "k_cardinal_fallback_h must cover every Direction value");
    static_assert(k_cardinal_fallback_v.size() == core::k_num_directions,
                  "k_cardinal_fallback_v must cover every Direction value");

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

    /// Whether the velocity at dense slot @p transform_slot says the entity has moved this step.
    [[nodiscard]] bool is_moving(const TransformTable &transforms, std::uint32_t transform_slot) noexcept {
      return std::abs(transforms.dc[transform_slot]) > k_motion_speed_epsilon ||
             std::abs(transforms.dr[transform_slot]) > k_motion_speed_epsilon;
    }

    /// Put @p e back on the sheet its clip interrupted, and drop the clip.
    void resume_after_clip(SpriteTable &sprites, AnimationTable &animations, const FacingTable &facings,
                           ClipTable &clips, EntityId e) noexcept {
      sprites.sprite_id_ref(e) = clips.resume_sprite_of(e);
      animations.set_frame_counts(e, clips.resume_counts_of(e));
      animations.timer_ref(e) = 0.f;
      sprites.frame_index_ref(e) = 0;
      sprites.anim_id_ref(e) = pick_target_anim(animations, facings, e, /*moving=*/false, Direction::South);
      clips.remove(e);
    }

    /// Advance @p e's clip by @p dt; returns true on the step it plays past its last frame.
    [[nodiscard]] bool advance_clip(AnimationTable &animations, const ClipTable &clips, EntityId e,
                                    uint8_t &frame_index, uint8_t frame_count, float dt) noexcept {
      const float frame_duration = clips.frame_duration_of(e);
      float &timer = animations.timer_ref(e);
      timer += dt;
      while (timer >= frame_duration) {
        timer -= frame_duration;
        if (frame_index + 1 >= frame_count)
          return true;
        ++frame_index;
      }
      return false;
    }

  } // namespace

  void update(SpriteTable &sprites, const TransformTable &transforms, AnimationTable &animations, FacingTable &facings,
              MotionSpriteTable &motion_sprites, const ClipTable &clips, core::math::IsometricParams iso,
              float reference_speed, float dt) noexcept {
    for (uint32_t i = 0; i < animations.count; ++i) {
      const EntityId e = animations.index.entities[i];
      if (clips.has(e)) [[unlikely]]
        continue;

      if (!sprites.has(e) || !transforms.has(e)) [[unlikely]]
        continue;

      const uint32_t transform_slot = transforms.dense_index(e);

      AnimId &sprite_anim_id = sprites.anim_id_ref(e);
      uint8_t &sprite_frame_index = sprites.frame_index_ref(e);
      float &animation_timer = animations.timer[i];

      const float vel_dc = transforms.dc[transform_slot];
      const float vel_dr = transforms.dr[transform_slot];

      const bool moving = is_moving(transforms, transform_slot);

      const float speed_scale = compute_speed_scale(moving, vel_dc, vel_dr, reference_speed, iso);

      tick_motion_sprite(sprites, animations, motion_sprites, e, moving, dt);

      const Direction facing = moving ? core::direction_from_tile_delta(vel_dc, vel_dr) : Direction::South;

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

  void play_clip(SpriteTable &sprites, AnimationTable &animations, const FacingTable &facings, ClipTable &clips,
                 EntityId e, const Clip &clip) noexcept {
    assert(clip.frame_duration > 0.f);
    if (clips.has(e))
      clips.replace(e, clip);
    else
      clips.insert(e, clip, sprites.sprite_id_ref(e), animations.frame_counts_of(e));
    sprites.sprite_id_ref(e) = clip.sprite;
    animations.set_frame_counts(e, clip.frame_counts);
    animations.timer_ref(e) = 0.f;
    sprites.frame_index_ref(e) = 0;
    sprites.anim_id_ref(e) = pick_target_anim(animations, facings, e, /*moving=*/false, Direction::South);
  }

  void update_clips(SpriteTable &sprites, const TransformTable &transforms, AnimationTable &animations,
                    const FacingTable &facings, ClipTable &clips, float dt) noexcept {
    // Backwards: ending a Resume clip swaps the last row, already processed, into this one.
    for (std::uint32_t i = clips.count; i-- > 0;) {
      const EntityId e = clips.index.entities[i];
      if (clips.is_finished[i])
        continue;

      const bool resumes = clips.end[i] == ClipEnd::Resume;
      const bool moving = is_moving(transforms, transforms.dense_index(e));
      if (resumes && moving) {
        resume_after_clip(sprites, animations, facings, clips, e);
        continue;
      }

      const AnimId anim = pick_target_anim(animations, facings, e, /*moving=*/false, Direction::South);
      sprites.anim_id_ref(e) = anim;
      if (!advance_clip(animations, clips, e, sprites.frame_index_ref(e), animations.frame_count(e, anim), dt))
        continue;
      if (resumes)
        resume_after_clip(sprites, animations, facings, clips, e);
      else
        clips.mark_finished(e);
    }
  }

} // namespace corundum::animation
