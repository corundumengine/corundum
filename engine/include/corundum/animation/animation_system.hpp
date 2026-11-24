// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/entity.hpp>

namespace corundum::entities {
struct AnimationTable;
struct Clip;
struct ClipTable;
struct FacingTable;
struct MotionSpriteTable;
struct SpriteTable;
struct TransformTable;
} // namespace corundum::entities

namespace corundum::animation {

/** @brief Advance all animated sprites by one fixed timestep.
 *
 * Reads velocity from @p transforms to select facing and the walk/idle state, advances
 * the walk/idle sheet transition in @p motion_sprites (honouring its configured delay),
 * selects the clip for the resolved facing, and advances the frame index on @p sprites.
 *
 * While moving, playback is scaled by the entity's actual screen-space speed relative
 * to @p reference_speed, so walk cycles stay in sync and future speed modifiers
 * (sprint, slow zones) are reflected here automatically. Idle entities and degenerate
 * @p iso / @p reference_speed play at the unscaled rate. Clip selection falls back from
 * the facing clip to the dominant-axis cardinal clip, then to AnimId::Default.
 *
 * @param[in,out] sprites         sprite_id, anim_id, and frame_index are updated.
 * @param[in]     transforms      velocity fields select facing and walk/idle state.
 * @param[in,out] animations      timers and frame counts are updated; frame duration supplies the base rate.
 * @param[in,out] facings         direction is overwritten while moving.
 * @param[in,out] motion_sprites  pending walk/idle sheet transitions are advanced.
 * @param[in]     clips           entities update_clips is playing.
 * @param[in]     iso             projection params converting velocity to screen speed.
 * @param[in]     reference_speed screen speed (px/s) at which a clip plays at its authored rate.
 * @param[in]     dt              fixed timestep in seconds.
 * @post Entities with a @p motion_sprites entry show the walk sheet while moving and the
 *       idle sheet while still, once the configured transition delay elapses.
 *       Entities with a row in @p clips are skipped; update_clips plays them.
 * Performance: O(n) over active animation count. No heap allocation.
 */
void update(corundum::entities::SpriteTable &sprites, const corundum::entities::TransformTable &transforms,
            corundum::entities::AnimationTable &animations, corundum::entities::FacingTable &facings,
            corundum::entities::MotionSpriteTable &motion_sprites, const corundum::entities::ClipTable &clips,
            corundum::core::math::IsometricParams iso, float reference_speed, float dt) noexcept;

/** @brief Start @p clip on @p e, on the frame facing the way @p e faces. A clip started over
 *  another keeps the sheet that one will return to. A ClipEnd::Hold clip stays on its last frame
 *  until stop_clip.
 *  @pre @p e has transform, sprite and animation rows, and @p clip has a positive frame_duration. */
void play_clip(corundum::entities::SpriteTable &sprites, corundum::entities::AnimationTable &animations,
               const corundum::entities::FacingTable &facings, corundum::entities::ClipTable &clips,
               corundum::entities::EntityId e, const corundum::entities::Clip &clip) noexcept;

/** @brief End @p e's clip now, putting it back on the sheet the clip interrupted.
 *
 *  A finished ClipEnd::Hold clip leaves its last frame this way, and a game can end any clip
 *  early instead of waiting for the movement that ends a Resume one.
 *  @param[in,out] sprites    sprite_id, anim_id and frame_index are restored.
 *  @param[in,out] animations frame counts and timer are restored.
 *  @param[in]     facings    selects the anim_id of the restored sheet.
 *  @param[in,out] clips      @p e's row is removed.
 *  @pre @p e has sprite and animation rows when it has a clip row; play_clip established them.
 *  @note An entity with no clip row is a no-op. Ending a ClipEnd::Remove clip cancels the removal
 *        it would have caused, keeping the entity. */
void stop_clip(corundum::entities::SpriteTable &sprites, corundum::entities::AnimationTable &animations,
               const corundum::entities::FacingTable &facings, corundum::entities::ClipTable &clips,
               corundum::entities::EntityId e) noexcept;

/** @brief Advance every playing clip by one fixed timestep, at its own frame_duration.
 *
 *  A Resume clip ends after its last frame, or as soon as its entity moves, and the entity goes
 *  back to the sheet it interrupted. A Hold or Remove clip stops on its last frame and is marked
 *  finished, moving or not. A clip's frames follow the entity's facing, with the same fallback
 *  as update.
 *  @param[in,out] sprites    sprite_id, anim_id and frame_index are updated.
 *  @param[in]     transforms velocity tells whether an entity has moved.
 *  @param[in,out] animations timers are updated, and frame counts when a clip ends.
 *  @param[in]     facings    the direction each clip plays.
 *  @param[in,out] clips      rows are marked finished, or removed when a Resume clip ends.
 *  @param[in]     dt         fixed timestep in seconds.
 *  @pre Clip rows come from play_clip, so each entity has its transform, sprite and animation
 *       rows.
 *  Performance: O(n) over playing clips. No heap allocation. */
void update_clips(corundum::entities::SpriteTable &sprites, const corundum::entities::TransformTable &transforms,
                  corundum::entities::AnimationTable &animations, const corundum::entities::FacingTable &facings,
                  corundum::entities::ClipTable &clips, float dt) noexcept;

} // namespace corundum::animation
