#include <corundum/animation/animation_system.hpp>

#include <corundum/core/direction.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cmath>
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
    using corundum::sprites::SpriteId;
    using corundum::sprites::to_anim;

    inline constexpr std::array<Direction, 12> k_facing_table = {
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
    inline constexpr std::array<AnimId, core::k_num_directions> k_cardinal_fallback_h = {
        AnimId::South, AnimId::North, AnimId::East, AnimId::West,
        AnimId::East,  AnimId::East,  AnimId::West, AnimId::West,
    };

    inline constexpr std::array<AnimId, core::k_num_directions> k_cardinal_fallback_v = {
        AnimId::South, AnimId::North, AnimId::East,  AnimId::West,
        AnimId::North, AnimId::South, AnimId::South, AnimId::North,
    };

    /// Ratio above which the dominant velocity axis snaps fully cardinal (zone 0 or 1)
    /// instead of remaining diagonal (zone 2).
    inline constexpr float k_cardinal_dominance_ratio = 2.f;

    // Tying the fallback tables to the Count sentinel makes any future addition to
    // Direction a hard compile error rather than a silent out-of-bounds index.
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
      int zone = 2;
      if (abs_dy > k_cardinal_dominance_ratio * abs_dx) {
        zone = 0;
      } else if (abs_dx > k_cardinal_dominance_ratio * abs_dy) {
        zone = 1;
      }
      const int dy_sign = vel_dy > 0.f ? 1 : 0;
      const int dx_sign = vel_dx > 0.f ? 1 : 0;
      return k_facing_table[(zone * 4) + (dy_sign * 2) + dx_sign];
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
     * Updates cur_sid, frame counts, frame index, and timer when a transition
     * commits. No-op when @p e is not in @p motion_sprites or no transition is
     * pending.
     */
    void tick_motion_sprite(SpriteTable &sprites, AnimationTable &animations, MotionSpriteTable &motion_sprites,
                            EntityId e, bool moving, float dt, uint8_t &spr_frame_idx, float &anim_timer) noexcept {
      if (!motion_sprites.has(e)) [[likely]]
        return;

      const SpriteId desired = moving ? motion_sprites.walk_sprite(e) : motion_sprites.idle_sprite(e);
      SpriteId &cur_sid = sprites.sprite_id_ref(e);
      if (cur_sid == desired) {
        motion_sprites.cancel_transition(e);
        return;
      }

      if (motion_sprites.pending_sprite(e) != desired)
        motion_sprites.set_pending(e, desired);

      const float elapsed = motion_sprites.tick_transition(e, dt);
      if (elapsed < motion_sprites.delay_for(e, desired)) [[likely]]
        return;

      cur_sid = desired;
      animations.set_frame_counts(e,
                                  moving ? motion_sprites.walk_frame_counts(e) : motion_sprites.idle_frame_counts(e));
      const float fd = motion_sprites.frame_duration_for(e, desired);
      if (fd > 0.f)
        animations.frame_duration_ref(e) = fd;
      spr_frame_idx = 0;
      anim_timer = 0.f;
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
      Direction fd = Direction::South;
      if (moving) {
        fd = moving_facing;
      } else if (facings.has(e)) {
        fd = facings.dir_of(e);
      }
      const AnimId dir_anim = to_anim(fd);
      if (animations.frame_count(e, dir_anim) > 0)
        return dir_anim;
      const auto fdx = std::to_underlying(fd);
      const AnimId fallback = (abs_dx >= abs_dy) ? k_cardinal_fallback_h[fdx] : k_cardinal_fallback_v[fdx];
      return animations.frame_count(e, fallback) > 0 ? fallback : AnimId::Default;
    }

    /** @brief Advance the playback timer and wrap the frame index when it crosses a frame.
     *
     * No-op when the clip has 0 or 1 frame (no animation possible).
     */
    void advance_frame_timer(float &anim_timer, float anim_fd, uint8_t frame_count, uint8_t &spr_frame_idx, float dt,
                             float speed_scale) noexcept {
      if (frame_count <= 1) [[unlikely]]
        return;
      anim_timer += dt * speed_scale;
      if (anim_timer >= anim_fd) {
        anim_timer -= anim_fd;
        spr_frame_idx = static_cast<uint8_t>((spr_frame_idx + 1) % frame_count);
      }
    }

  } // namespace

  void update(SpriteTable &sprites, const TransformTable &transforms, AnimationTable &animations, FacingTable &facings,
              MotionSpriteTable &motion_sprites, core::math::IsometricParams iso, float reference_speed,
              float dt) noexcept {
    [[assume(animations.count <= AnimationTable::k_max)]];
    float *const timers = std::assume_aligned<16>(animations.timer.data());
    const float *const frame_durations = std::assume_aligned<16>(animations.frame_duration.data());
    for (uint32_t i = 0; i < animations.count; ++i) {
      const EntityId e = animations.idx.entities[i];
      if (!sprites.has(e) || !transforms.has(e)) [[unlikely]]
        continue;

      const uint32_t tr_slot = transforms.dense_idx(e);

      AnimId &spr_anim_id = sprites.anim_id_ref(e);
      uint8_t &spr_frame_idx = sprites.frame_index_ref(e);
      float &anim_timer = timers[i];
      const float &anim_fd = frame_durations[i];

      const float &vel_dx = transforms.dc[tr_slot];
      const float &vel_dy = transforms.dr[tr_slot];

      const bool moving = (vel_dx != 0.f || vel_dy != 0.f);
      const float abs_dx = std::abs(vel_dx);
      const float abs_dy = std::abs(vel_dy);

      const float speed_scale = compute_speed_scale(moving, vel_dx, vel_dy, reference_speed, iso);

      tick_motion_sprite(sprites, animations, motion_sprites, e, moving, dt, spr_frame_idx, anim_timer);

      const Direction facing = moving ? facing_from_velocity(abs_dx, abs_dy, vel_dx, vel_dy) : Direction::South;

      if (moving && facings.has(e)) [[likely]]
        facings.dir_ref(e) = facing;

      const AnimId target = pick_target_anim(animations, facings, e, moving, facing, abs_dx, abs_dy);

      if (spr_anim_id != target) [[unlikely]] {
        spr_anim_id = target;
        spr_frame_idx = 0;
        anim_timer = 0.f;
      }

      const uint8_t frame_count = animations.frame_count(e, spr_anim_id);
      advance_frame_timer(anim_timer, anim_fd, frame_count, spr_frame_idx, dt, speed_scale);
    }
  }

} // namespace corundum::animation
