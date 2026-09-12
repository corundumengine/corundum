#pragma once
#include <corundum/core/math/vec.hpp>

namespace corundum::entities {
  struct AnimationTable;
  struct FacingTable;
  struct MotionSpriteTable;
  struct SpriteTable;
  struct TransformTable;
} // namespace corundum::entities

namespace corundum::animation {

  /** @brief Advance all animated sprites by one fixed timestep.
   *
   * Reads velocity from @p transforms to resolve movement and facing, advances the
   * walk/idle sheet transition in @p motion_sprites (honouring its configured delay),
   * selects the clip for the resolved facing, and advances the frame index on @p sprites.
   *
   * While moving, playback is scaled by the entity's actual screen-space speed relative
   * to @p reference_speed, so walk cycles stay in sync and future speed modifiers
   * (sprint, slow zones) are reflected here automatically. Idle entities and degenerate
   * @p iso / @p reference_speed play at the unscaled rate. Clip selection falls back from
   * the facing clip to the dominant-axis cardinal clip, then to AnimId::Default.
   *
   *  @param[in,out] sprites        sprite_id, anim_id, and frame_index are updated.
   *  @param[in]     transforms     velocity fields drive movement and facing.
   *  @param[in,out] animations     timers, frame counts, and frame durations are updated.
   *  @param[in,out] facings        direction is overwritten while moving.
   *  @param[in,out] motion_sprites pending walk/idle sheet transitions are advanced.
   *  @param[in]     iso            projection params converting velocity to screen speed.
   *  @param[in]     reference_speed screen speed (px/s) at which a clip plays at its authored rate.
   *  @param[in]     dt             fixed timestep in seconds.
   *  @post Active sprite is the walk sheet while moving, idle sheet while still.
   *  @performance O(n) over active animation count. No heap allocation.
   */
  void update(corundum::entities::SpriteTable &sprites, const corundum::entities::TransformTable &transforms,
              corundum::entities::AnimationTable &animations, corundum::entities::FacingTable &facings,
              corundum::entities::MotionSpriteTable &motion_sprites, corundum::core::math::IsometricParams iso,
              float reference_speed, float dt) noexcept;

} // namespace corundum::animation
