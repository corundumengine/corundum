#include <corundum/anim/anim_sys.hpp>

#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cmath>
#include <memory>
#include <utility>

namespace corundum::anim {

  namespace {
    using corundum::entities::FacingDir;
    using corundum::sprites::AnimId;

    inline constexpr std::array<FacingDir, 12> k_facing_table = {
        // zone 0 (row/vertical dominant: |dr| >> |dc|)
        // row axis (dr) maps to NE/SW on screen; entries here use FacingDir in screen-space terms.
        FacingDir::NorthEast, // dr<0, dc<0  (NE-ish on screen)
        FacingDir::NorthEast, // dr<0, dc>0  (NE-ish on screen)
        FacingDir::SouthWest, // dr>0, dc<0  (SW-ish on screen)
        FacingDir::SouthWest, // dr>0, dc>0  (SW-ish on screen)
        // zone 1 (col/horizontal dominant: |dc| >> |dr|)
        // col axis (dc) maps to NW/SE on screen.
        FacingDir::NorthWest, // dc<0, dr<0  (NW-ish on screen)
        FacingDir::SouthEast, // dc>0, dr<0  (SE-ish on screen)
        FacingDir::NorthWest, // dc<0, dr>0  (NW-ish on screen)
        FacingDir::SouthEast, // dc>0, dr>0  (SE-ish on screen)
        // zone 2 (diagonal: |dc| ≈ |dr|)
        // Pure screen-cardinal directions from combined tile axes.
        FacingDir::North, // dr<0, dc<0  (NW tile = up on screen)
        FacingDir::East,  // dr<0, dc>0  (NE tile = right on screen)
        FacingDir::West,  // dr>0, dc<0  (SW tile = left on screen)
        FacingDir::South, // dr>0, dc>0  (SE tile = down on screen)
    };

    inline constexpr std::array<AnimId, 8> k_anim_for_facing = {
        AnimId::South,     AnimId::North,     AnimId::East,      AnimId::West,
        AnimId::NorthEast, AnimId::SouthEast, AnimId::SouthWest, AnimId::NorthWest,
    };

    inline constexpr std::array<AnimId, 8> k_cardinal_fallback_h = {
        AnimId::South, AnimId::North, AnimId::East, AnimId::West,
        AnimId::East,  AnimId::East,  AnimId::West, AnimId::West,
    };

    inline constexpr std::array<AnimId, 8> k_cardinal_fallback_v = {
        AnimId::South, AnimId::North, AnimId::East,  AnimId::West,
        AnimId::North, AnimId::South, AnimId::South, AnimId::North,
    };

    /// Ratio above which the dominant velocity axis snaps fully cardinal (zone 0 or 1)
    /// instead of remaining diagonal (zone 2).
    inline constexpr float k_cardinal_dominance_ratio = 2.f;

    // Lookup tables above are indexed by std::to_underlying(FacingDir). Pinning
    // their cardinality here makes any future addition to FacingDir (e.g. a 16-way
    // direction) a hard compile error rather than a silent mis-index.
    static_assert(k_anim_for_facing.size() == 8, "k_anim_for_facing must cover every FacingDir value");
    static_assert(k_cardinal_fallback_h.size() == 8, "k_cardinal_fallback_h must cover every FacingDir value");
    static_assert(k_cardinal_fallback_v.size() == 8, "k_cardinal_fallback_v must cover every FacingDir value");

  } // namespace

  void animate(corundum::entities::SpriteTable &sprites, const corundum::entities::TransformTable &transforms,
               corundum::entities::AnimationTable &animations, corundum::entities::FacingTable &facings,
               corundum::entities::MotionSpriteTable &motion_sprites, corundum::core::math::IsometricParams iso,
               float reference_speed, float dt) noexcept {
    using corundum::entities::AnimationTable;
    using corundum::entities::EntityId;
    using corundum::entities::FacingTable;
    using corundum::entities::MotionSpriteTable;
    using corundum::entities::SpriteTable;
    using corundum::entities::TransformTable;
    using corundum::sprites::AnimId;
    using corundum::sprites::SpriteId;

    [[assume(animations.count <= std::remove_reference_t<decltype(animations)>::k_max)]];
    float *const timers = std::assume_aligned<16>(animations.timer.data());
    const float *const frame_durations = std::assume_aligned<16>(animations.frame_duration.data());
    for (uint16_t i = 0; i < animations.count; ++i) {
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

      // Idle has zero velocity by definition — must NOT scale to 0 (that would freeze
      // the idle animation). Only scale while actually moving, and guard reference_speed/iso
      // being degenerate (misconfigured) so this never divides by zero or propagates NaN.
      float speed_scale = 1.f;
      if (moving && reference_speed > 0.f && iso.half_tw > 0.f && iso.half_th > 0.f) {
        const auto [svx, svy] = corundum::core::math::tile_to_screen_delta(vel_dx, vel_dy, iso);
        speed_scale = std::hypot(svx, svy) / reference_speed;
      }

      if (motion_sprites.has(e)) [[unlikely]] {
        const SpriteId desired = moving ? motion_sprites.walk_sprite(e) : motion_sprites.idle_sprite(e);
        SpriteId &cur_sid = sprites.sprite_id_ref(e);
        if (cur_sid == desired) {
          motion_sprites.cancel_transition(e);
        } else {
          if (motion_sprites.pending_sprite(e) != desired)
            motion_sprites.set_pending(e, desired);
          const float elapsed = motion_sprites.tick_transition(e, dt);
          if (elapsed >= motion_sprites.delay_for(e, desired)) [[unlikely]] {
            cur_sid = desired;
            animations.set_frame_counts(e, moving ? motion_sprites.walk_frame_counts(e)
                                                  : motion_sprites.idle_frame_counts(e));
            const float fd = motion_sprites.frame_duration_for(e, desired);
            if (fd > 0.f)
              animations.frame_duration_ref(e) = fd;
            spr_frame_idx = 0;
            anim_timer = 0.f;
            motion_sprites.cancel_transition(e);
          }
        }
      }

      const int zone =
          (abs_dy > k_cardinal_dominance_ratio * abs_dx) ? 0 : ((abs_dx > k_cardinal_dominance_ratio * abs_dy) ? 1 : 2);
      const int dy_sign = vel_dy > 0.f ? 1 : 0;
      const int dx_sign = vel_dx > 0.f ? 1 : 0;
      const FacingDir facing = k_facing_table[zone * 4 + dy_sign * 2 + dx_sign];

      if (moving && facings.has(e)) [[likely]]
        facings.dir_ref(e) = facing;

      const AnimId target = [&]() noexcept -> AnimId {
        const FacingDir fd = moving ? facing : (facings.has(e) ? facings.dir_of(e) : FacingDir::South);
        const AnimId dir_anim = k_anim_for_facing[std::to_underlying(fd)];
        if (animations.frame_count(e, dir_anim) > 0)
          return dir_anim;
        const AnimId fallback = (abs_dx >= abs_dy) ? k_cardinal_fallback_h[std::to_underlying(fd)]
                                                   : k_cardinal_fallback_v[std::to_underlying(fd)];
        return animations.frame_count(e, fallback) > 0 ? fallback : AnimId::Default;
      }();

      if (spr_anim_id != target) [[unlikely]] {
        spr_anim_id = target;
        spr_frame_idx = 0;
        anim_timer = 0.f;
      }

      const uint8_t frame_count = animations.frame_count(e, spr_anim_id);
      if (frame_count <= 1)
        continue;

      anim_timer += dt * speed_scale;
      if (anim_timer >= anim_fd) {
        anim_timer -= anim_fd;
        spr_frame_idx = (spr_frame_idx + 1) % frame_count;
      }
    }
  }

} // namespace corundum::anim
