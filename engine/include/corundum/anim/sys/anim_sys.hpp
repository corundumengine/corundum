#pragma once
#include <corundum/gameplay/component/animation_table.hpp>
#include <corundum/gameplay/component/facing_table.hpp>
#include <corundum/gameplay/component/motion_sprite_table.hpp>
#include <corundum/gameplay/component/sprite_table.hpp>
#include <corundum/gameplay/component/transform_table.hpp>

namespace corundum::anim::sys {

  /** @brief Advance all animated sprites by one fixed timestep.
   *
   * Reads velocity from @p transforms to determine movement state and facing
   * direction. Updates frame indices on @p sprites. Switches between walk and
   * idle sprite sheets via @p motion_sprites. Operates entirely on SoA data —
   * no heap allocation.
   *
   *  @param[in,out] sprites        SpriteTable; sprite_id, anim_id, and frame_index are updated.
   *  @param[in]     transforms     TransformTable; velocity fields drive facing and direction.
   *  @param[in,out] animations     AnimationTable; timers and cached frame counts are updated.
   *  @param[in,out] facings        FacingTable; direction is set from dominant velocity axis.
   *  @param[in,out] motion_sprites MotionSpriteTable; drives walk/idle sprite sheet switching.
   *  @param[in]     dt             Fixed timestep in seconds.
   *  @post Active sprite switched to walk sheet while moving, idle sheet while still.
   *  @post All active animation timers advanced by dt.
   *  @post Frame indices wrap according to cached frame counts.
   *  @performance O(n) over active animation count. No heap allocation.
   */
  void animate(corundum::gameplay::component::SpriteTable &sprites,
               const corundum::gameplay::component::TransformTable &transforms,
               corundum::gameplay::component::AnimationTable &animations,
               corundum::gameplay::component::FacingTable &facings,
               corundum::gameplay::component::MotionSpriteTable &motion_sprites, float dt) noexcept;

} // namespace corundum::anim::sys
