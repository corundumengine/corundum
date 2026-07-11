#include <corundum/anim/sys/anim_sys.hpp>
#include <corundum/gameplay/component/motion_sprite_table.hpp>
#include <corundum/resources/sprite.hpp>

#include <cmath>
#include <memory>
#include <utility>

namespace corundum::anim::sys {

  namespace {

    inline constexpr std::array<corundum::gameplay::component::FacingDir, 12> k_facing_table = {
        // zone 0 (row/vertical dominant: |dr| >> |dc|)
        // row axis (dr) maps to NE/SW on screen; entries here use FacingDir in screen-space terms.
        corundum::gameplay::component::FacingDir::NorthEast, // dr<0, dc<0  (NE-ish on screen)
        corundum::gameplay::component::FacingDir::NorthEast, // dr<0, dc>0  (NE-ish on screen)
        corundum::gameplay::component::FacingDir::SouthWest, // dr>0, dc<0  (SW-ish on screen)
        corundum::gameplay::component::FacingDir::SouthWest, // dr>0, dc>0  (SW-ish on screen)
        // zone 1 (col/horizontal dominant: |dc| >> |dr|)
        // col axis (dc) maps to NW/SE on screen.
        corundum::gameplay::component::FacingDir::NorthWest, // dc<0, dr<0  (NW-ish on screen)
        corundum::gameplay::component::FacingDir::SouthEast, // dc>0, dr<0  (SE-ish on screen)
        corundum::gameplay::component::FacingDir::NorthWest, // dc<0, dr>0  (NW-ish on screen)
        corundum::gameplay::component::FacingDir::SouthEast, // dc>0, dr>0  (SE-ish on screen)
        // zone 2 (diagonal: |dc| ≈ |dr|)
        // Pure screen-cardinal directions from combined tile axes.
        corundum::gameplay::component::FacingDir::North, // dr<0, dc<0  (NW tile = up on screen)
        corundum::gameplay::component::FacingDir::East,  // dr<0, dc>0  (NE tile = right on screen)
        corundum::gameplay::component::FacingDir::West,  // dr>0, dc<0  (SW tile = left on screen)
        corundum::gameplay::component::FacingDir::South, // dr>0, dc>0  (SE tile = down on screen)
    };

    inline constexpr std::array<corundum::resources::AnimId, 8> k_anim_for_facing = {
        corundum::resources::AnimId::South,     corundum::resources::AnimId::North,
        corundum::resources::AnimId::East,      corundum::resources::AnimId::West,
        corundum::resources::AnimId::NorthEast, corundum::resources::AnimId::SouthEast,
        corundum::resources::AnimId::SouthWest, corundum::resources::AnimId::NorthWest,
    };

    inline constexpr std::array<corundum::resources::AnimId, 8> k_cardinal_fallback_h = {
        corundum::resources::AnimId::South, corundum::resources::AnimId::North, corundum::resources::AnimId::East,
        corundum::resources::AnimId::West,  corundum::resources::AnimId::East,  corundum::resources::AnimId::East,
        corundum::resources::AnimId::West,  corundum::resources::AnimId::West,
    };

    inline constexpr std::array<corundum::resources::AnimId, 8> k_cardinal_fallback_v = {
        corundum::resources::AnimId::South, corundum::resources::AnimId::North, corundum::resources::AnimId::East,
        corundum::resources::AnimId::West,  corundum::resources::AnimId::North, corundum::resources::AnimId::South,
        corundum::resources::AnimId::South, corundum::resources::AnimId::North,
    };

    /// Ratio above which the dominant velocity axis snaps fully cardinal (zone 0 or 1)
    /// instead of remaining diagonal (zone 2).
    inline constexpr float k_cardinal_dominance_ratio = 2.f;

  } // namespace

  void animate(corundum::gameplay::component::SpriteTable &sprites,
               const corundum::gameplay::component::TransformTable &transforms,
               corundum::gameplay::component::AnimationTable &animations,
               corundum::gameplay::component::FacingTable &facings,
               corundum::gameplay::component::MotionSpriteTable &motion_sprites, float dt) noexcept {
    [[assume(animations.count <= std::remove_reference_t<decltype(animations)>::k_max)]];
    float *timers = std::assume_aligned<16>(animations.timer.data());
    const float *frame_durations = std::assume_aligned<16>(animations.frame_duration.data());
    for (uint16_t i = 0; i < animations.count; ++i) {
      const corundum::gameplay::entity::EntityId e = animations.idx.entities[i];
      if (!sprites.has(e) || !transforms.has(e)) [[unlikely]]
        continue;

      const auto anim_slot = i;
      const auto tr_slot = transforms.dense_idx(e);

      auto &spr_anim_id = sprites.anim_id_ref(e);
      auto &spr_frame_idx = sprites.frame_index_ref(e);
      auto &anim_timer = timers[anim_slot];
      const auto &anim_fd = frame_durations[anim_slot];

      const auto &vel_dx = transforms.dc[tr_slot];
      const auto &vel_dy = transforms.dr[tr_slot];

      const bool moving = (vel_dx != 0.f || vel_dy != 0.f);

      const float abs_dx = std::abs(vel_dx);
      const float abs_dy = std::abs(vel_dy);

      if (motion_sprites.has(e)) [[unlikely]] {
        const auto desired = moving ? motion_sprites.walk_sprite(e) : motion_sprites.idle_sprite(e);
        auto &cur_sid = sprites.sprite_id_ref(e);
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
      const corundum::gameplay::component::FacingDir facing = k_facing_table[zone * 4 + dy_sign * 2 + dx_sign];

      if (moving && facings.has(e)) [[likely]]
        facings.dir_ref(e) = facing;

      const corundum::resources::AnimId target = [&]() noexcept -> corundum::resources::AnimId {
        const corundum::gameplay::component::FacingDir fd =
            moving ? facing : (facings.has(e) ? facings.dir_of(e) : corundum::gameplay::component::FacingDir::South);
        const auto dir_anim = k_anim_for_facing[std::to_underlying(fd)];
        if (animations.frame_count(e, dir_anim) > 0)
          return dir_anim;
        const auto fallback = (abs_dx >= abs_dy) ? k_cardinal_fallback_h[std::to_underlying(fd)]
                                                 : k_cardinal_fallback_v[std::to_underlying(fd)];
        return animations.frame_count(e, fallback) > 0 ? fallback : corundum::resources::AnimId::Default;
      }();

      if (spr_anim_id != target) [[unlikely]] {
        spr_anim_id = target;
        spr_frame_idx = 0;
        anim_timer = 0.f;
      }

      const uint8_t frame_count = animations.frame_count(e, spr_anim_id);
      if (frame_count <= 1)
        continue;

      anim_timer += dt;
      if (anim_timer >= anim_fd) {
        anim_timer -= anim_fd;
        spr_frame_idx = (spr_frame_idx + 1) % frame_count;
      }
    }
  }

} // namespace corundum::anim::sys
