// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/direction.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <doctest/doctest.h>

#include <corundum/animation/animation_system.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/tables/facing_table.hpp>
#include <corundum/entities/tables/motion_sprite_table.hpp>
#include <corundum/entities/tables/sprite_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>

#include <array>
#include <utility>

using corundum::core::math::IsometricParams;
using corundum::entities::AnimationTable;
using corundum::entities::ClipTable;
using corundum::entities::EntityId;
using corundum::entities::FacingTable;
using corundum::entities::MotionSpriteTable;
using corundum::entities::SpriteTable;
using corundum::entities::TransformTable;

namespace {

  // half_tw/half_th form a 3-4-5 right triangle so every screen-speed ratio exercised
  // below is exact in floating point (no ulp slop flipping an exact >= threshold check).
  constexpr IsometricParams k_iso{.half_tw = 4.f, .half_th = 3.f, .x_origin = 0.f, .elev_step = 0.f};
  constexpr float k_reference_speed = 10.f; // screen px/s

  // Field order is the padding-optimal one (transform/animations/player/facings/sprites/
  // motion_sprites) rather than declaration order; the test only ever reads them by name.
  struct Fixture {
    TransformTable transforms;
    AnimationTable animations;
    EntityId player = static_cast<EntityId>(0);
    FacingTable facings;
    SpriteTable sprites;
    MotionSpriteTable motion_sprites;
    ClipTable clips;

    Fixture() {
      transforms.insert(player, 0.5f, 0.5f, 0.f, 0.f);
      sprites.insert(player, corundum::sprites::SpriteId{0}, corundum::sprites::AnimId::South, 0);
      animations.insert(player);
      std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
      counts.fill(4); // every clip has frames, regardless of which facing gets resolved
      animations.set_frame_counts(player, counts);
      animations.frame_duration_ref(player) = 0.2f;
    }
  };

  constexpr corundum::sprites::SpriteId k_clip_sprite{7};

  constexpr corundum::sprites::SpriteId k_other_clip_sprite{8};

  constexpr float k_clip_frame_seconds = 0.1f;

  /// A 3-frame clip: every AnimId has frames, so any facing resolves.
  corundum::entities::Clip make_clip(corundum::entities::ClipEnd end,
                                     corundum::sprites::SpriteId sprite = k_clip_sprite) {
    corundum::entities::Clip clip{
        .end = end,
        .frame_counts = {},
        .frame_duration = k_clip_frame_seconds,
        .sprite = sprite,
    };
    clip.frame_counts.fill(3);
    return clip;
  }

  void play(Fixture &f, EntityId e, const corundum::entities::Clip &clip) {
    corundum::animation::play_clip(f.sprites, f.animations, f.facings, f.clips, e, clip);
  }

  void run_clip_steps(Fixture &f, int steps) {
    for (int i = 0; i < steps; ++i)
      corundum::animation::update_clips(f.sprites, f.transforms, f.animations, f.facings, f.clips,
                                        k_clip_frame_seconds);
  }

  /// Give @p e a velocity, so a Resume clip sees it as moving.
  void move(Fixture &f, EntityId e) {
    f.transforms.dc[f.transforms.dense_index(e)] = 1.f;
  }

  /// A second animated entity, shaped like the fixture's player.
  EntityId add_animated_entity(Fixture &f, std::uint32_t id) {
    const auto e = static_cast<EntityId>(id);
    f.transforms.insert(e, 0.5f, 0.5f, 0.f, 0.f);
    f.sprites.insert(e, corundum::sprites::SpriteId{0}, corundum::sprites::AnimId::South, 0);
    f.animations.insert(e);
    std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
    counts.fill(4);
    f.animations.set_frame_counts(e, counts);
    return e;
  }

  constexpr corundum::sprites::SpriteId k_stance_walk_sprite{1};

  constexpr corundum::sprites::SpriteId k_stance_idle_sprite{2};

  constexpr corundum::sprites::SpriteId k_stance_ready_sprite{3};

  constexpr float k_stance_idle_frame_seconds = 0.2f;

  constexpr float k_stance_ready_frame_seconds = 0.3f;

  /// Give @p e a motion row with distinct walk, idle and ready sheets, standing on idle.
  void add_stance_row(Fixture &f, EntityId e) {
    std::array<uint8_t, corundum::sprites::k_num_anim_ids> walk_and_idle_counts{};
    walk_and_idle_counts.fill(4);
    std::array<uint8_t, corundum::sprites::k_num_anim_ids> ready_counts{};
    ready_counts.fill(6);
    f.motion_sprites.insert(e, MotionSpriteTable::Config{
                                   .walk_id = k_stance_walk_sprite,
                                   .idle_id = k_stance_idle_sprite,
                                   .walk_counts = walk_and_idle_counts,
                                   .idle_counts = walk_and_idle_counts,
                                   .idle_to_walk_delay = 0.f,
                                   .walk_to_idle_delay = 0.f,
                                   .walk_frame_duration = 0.f,
                                   .idle_frame_duration = k_stance_idle_frame_seconds,
                                   .ready_id = k_stance_ready_sprite,
                                   .ready_counts = ready_counts,
                                   .ready_frame_duration = k_stance_ready_frame_seconds,
                               });
    f.sprites.sprite_id_ref(e) = k_stance_idle_sprite;
  }

  void run_motion_step(Fixture &f) {
    corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                                k_reference_speed, /*dt=*/0.01f);
  }

} // namespace

TEST_CASE("animate: moving at exactly reference speed advances at the authored frame_duration") {
  Fixture f;
  const auto slot = f.transforms.dense_index(f.player);
  // Pure +col move at exactly the reference screen speed: |screen_vel| = 2*hypot(4,3) = 10.
  f.transforms.dc[slot] = 2.f; // scale = 1
  f.transforms.dr[slot] = 0.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1); // one full frame_duration elapsed at scale 1
}

TEST_CASE("animate: moving at 2x reference speed reaches the frame threshold in half the dt") {
  Fixture f;
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 4.f; // |screen_vel| = 20 → scale = 2
  f.transforms.dr[slot] = 0.f;

  // dt=0.1s * scale=2 == 0.2s == frame_duration: crosses the threshold; at scale=1 it would not.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.1f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: idle (zero velocity) still advances at the unscaled rate, not frozen") {
  Fixture f; // dc=dr=0 from the fixture — must not scale to 0
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: sub-epsilon velocity is treated as still") {
  Fixture f;
  f.facings.insert(f.player, corundum::core::Direction::North);
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 5e-5f; // below k_motion_speed_epsilon

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.2f);
  // Unscaled idle playback, and the stored facing is left untouched.
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
  CHECK(f.facings.dir_of(f.player) == corundum::core::Direction::North);
}

TEST_CASE("animate: degenerate iso falls back to the unscaled rate (no divide-by-zero/NaN)") {
  Fixture f;
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 5.f; // nonzero velocity, but iso below is degenerate
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips,
                              IsometricParams{.half_tw = 0.f, .half_th = 0.f, .x_origin = 0.f, .elev_step = 0.f},
                              k_reference_speed, /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: pure +dc motion resolves facing to SouthEast (zone 1, col-dominant)") {
  Fixture f;
  f.facings.insert(f.player, corundum::core::Direction::South);
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::core::Direction::SouthEast);
}

TEST_CASE("animate: pure +dr motion resolves facing to SouthWest (zone 0, row-dominant)") {
  Fixture f;
  f.facings.insert(f.player, corundum::core::Direction::South);
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 0.f;
  f.transforms.dr[slot] = 1.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::core::Direction::SouthWest);
}

TEST_CASE("animate: equal-magnitude diagonal motion resolves facing to a screen-cardinal (zone 2)") {
  Fixture f;
  f.facings.insert(f.player, corundum::core::Direction::South);
  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 1.f; // |dc| == |dr| → zone 2 (diagonal); +dr,+dc → screen South

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::core::Direction::South);
}

TEST_CASE("animate: falls back to a cardinal AnimId when the directional clip is empty") {
  Fixture f;
  // Empty every clip except the East cardinal, so a diagonal SouthEast resolve
  // (from pure +dc motion) must walk back to its horizontal-fallback AnimId::East.
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::sprites::AnimId::East)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::sprites::AnimId::East);
}

TEST_CASE("animate: vertical-axis fallback is used when |dr| dominates") {
  Fixture f;
  // Empty every clip except the South cardinal. Pure +dr motion picks facing=SouthWest,
  // which is empty → falls back along the dominant vertical axis → AnimId::South.
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::sprites::AnimId::South)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 0.f;
  f.transforms.dr[slot] = 1.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::sprites::AnimId::South);
}

TEST_CASE("animate: returns AnimId::Default when every directional and cardinal clip is empty") {
  Fixture f;
  const std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{}; // all zero
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::sprites::AnimId::Default);
}

TEST_CASE("animate: idle entity with no FacingTable uses the South default for AnimId lookup") {
  // Fixture has no FacingTable entry — `facings.has(e)` is false, so the resolve
  // falls back to Direction::South while idle. Without the default, accessing
  // `facings.dir_of(e)` would assert.
  Fixture f; // no facings.insert; counts.fill(4) leaves every clip available.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::sprites::AnimId::South);
}

TEST_CASE("animate: changing AnimId resets frame_index and timer") {
  Fixture f;
  // Build up a non-zero frame state.
  f.animations.frame_duration_ref(f.player) = 0.1f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.5f);
  REQUIRE(f.sprites.frame_index_ref(f.player) != 0);

  // Now restrict counts to AnimId::East only and start moving → South→East transition
  // must zero frame_index and timer.
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::sprites::AnimId::East)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::sprites::AnimId::East);
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
  CHECK(f.animations.timer_ref(f.player) == 0.f);
}

TEST_CASE("animate: single-frame AnimIds do not advance or accumulate timer") {
  Fixture f;
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts.fill(1); // every clip is a static image
  f.animations.set_frame_counts(f.player, counts);

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/10.f); // huge dt
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
  CHECK(f.animations.timer_ref(f.player) == 0.f);
}

TEST_CASE("animate: motion sprite commits after the configured idle-to-walk delay") {
  Fixture f;
  constexpr corundum::sprites::SpriteId k_walk_sprite_id{1};
  constexpr corundum::sprites::SpriteId k_idle_sprite_id{2};
  // Start with the idle sprite active so the transition is observable from the
  // initial state (the fixture otherwise initialises sprite_id to 0).
  f.sprites.sprite_id_ref(f.player) = k_idle_sprite_id;
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts.fill(4);
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = k_walk_sprite_id,
                                        .idle_id = k_idle_sprite_id,
                                        .walk_counts = counts,
                                        .idle_counts = counts,
                                        .idle_to_walk_delay = 0.1f,
                                        .walk_to_idle_delay = 0.f,
                                    });

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;

  // Half the delay — not yet committed; pending is set.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.05f);
  CHECK(f.sprites.sprite_id_ref(f.player) == k_idle_sprite_id);
  CHECK(f.motion_sprites.pending_sprite(f.player) == k_walk_sprite_id);

  // Cumulative dt reaches the delay — commits.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.05f);
  CHECK(f.sprites.sprite_id_ref(f.player) == k_walk_sprite_id);
  CHECK(f.motion_sprites.pending_sprite(f.player) == corundum::sprites::k_null_sprite_id);
}

TEST_CASE("animate: motion sprite cancels a pending transition when motion state flips before delay") {
  Fixture f;
  constexpr corundum::sprites::SpriteId k_walk_sprite_id{1};
  constexpr corundum::sprites::SpriteId k_idle_sprite_id{2};
  f.sprites.sprite_id_ref(f.player) = k_idle_sprite_id;
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts.fill(4);
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = k_walk_sprite_id,
                                        .idle_id = k_idle_sprite_id,
                                        .walk_counts = counts,
                                        .idle_counts = counts,
                                        .idle_to_walk_delay = 0.1f,
                                        .walk_to_idle_delay = 0.f,
                                    });

  const auto slot = f.transforms.dense_index(f.player);

  // Start moving, partial-transition the timer toward the idle→walk commit.
  f.transforms.dc[slot] = 1.f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.05f);
  REQUIRE(f.motion_sprites.pending_sprite(f.player) == k_walk_sprite_id);

  // Stop moving — desired flips back to idle, which is already cur_sid → cancel.
  f.transforms.dc[slot] = 0.f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.05f);
  CHECK(f.motion_sprites.pending_sprite(f.player) == corundum::sprites::k_null_sprite_id);
  CHECK(f.sprites.sprite_id_ref(f.player) == k_idle_sprite_id);
}

TEST_CASE("animate: motion sprite commit refreshes AnimationTable frame counts") {
  Fixture f;
  constexpr corundum::sprites::SpriteId k_walk_sprite_id{1};
  constexpr corundum::sprites::SpriteId k_idle_sprite_id{2};
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> walk_counts{};
  walk_counts.fill(6);
  walk_counts[static_cast<uint8_t>(corundum::sprites::AnimId::South)] = 8; // distinguish from idle
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> idle_counts{};
  idle_counts.fill(4);
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = k_walk_sprite_id,
                                        .idle_id = k_idle_sprite_id,
                                        .walk_counts = walk_counts,
                                        .idle_counts = idle_counts,
                                    });

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 1.f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f);

  CHECK(f.sprites.sprite_id_ref(f.player) == k_walk_sprite_id);
  CHECK(f.animations.frame_count(f.player, corundum::sprites::AnimId::South) == 8);
}

TEST_CASE("animate: motion sprite commit applies the new sheet's frame-duration override") {
  Fixture f;
  f.animations.frame_duration_ref(f.player) = 0.2f; // base rate
  constexpr corundum::sprites::SpriteId k_walk_sprite_id{1};
  constexpr corundum::sprites::SpriteId k_idle_sprite_id{2};
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts.fill(6);
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = k_walk_sprite_id,
                                        .idle_id = k_idle_sprite_id,
                                        .walk_counts = counts,
                                        .idle_counts = counts,
                                        .walk_frame_duration = 0.05f,
                                    });

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 2.f; // screen speed 10 == reference → scale 1
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f); // commits the idle→walk sheet

  // 0.06s at the 0.05s override advances one frame; the 0.2s base rate would not.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.06f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: idle commit with no override falls back to the AnimationTable base rate") {
  Fixture f;
  f.animations.frame_duration_ref(f.player) = 0.2f; // base rate
  constexpr corundum::sprites::SpriteId k_walk_sprite_id{1};
  constexpr corundum::sprites::SpriteId k_idle_sprite_id{2};
  std::array<uint8_t, corundum::sprites::k_num_anim_ids> counts{};
  counts.fill(6);
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = k_walk_sprite_id,
                                        .idle_id = k_idle_sprite_id,
                                        .walk_counts = counts,
                                        .idle_counts = counts,
                                        .walk_frame_duration = 0.1f, // idle sheet has no override
                                    });

  const auto slot = f.transforms.dense_index(f.player);
  f.transforms.dc[slot] = 2.f; // scale 1
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f); // commits the idle→walk sheet
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.12f); // 0.1s walk override → one frame
  REQUIRE(f.sprites.frame_index_ref(f.player) == 1);

  f.transforms.dc[slot] = 0.f;
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.f); // commits walk→idle and resets the frame index
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.12f); // base 0.2 → not yet a frame
  // The walk override must not leak into idle playback.
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
}

TEST_CASE("animate: one tick advances every whole frame that elapsed") {
  Fixture f;
  f.animations.frame_duration_ref(f.player) = 0.1f;

  // 2.5 frame durations elapse in a single idle tick: two whole frames, 0.05s residual.
  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.25f);
  CHECK(f.sprites.frame_index_ref(f.player) == 2);
  CHECK(f.animations.timer_ref(f.player) == doctest::Approx(0.05f));
}

TEST_CASE("animate: frame index wraps modulo the clip length") {
  Fixture f;
  f.animations.frame_duration_ref(f.player) = 0.1f;
  f.sprites.frame_index_ref(f.player) = 3; // last frame of the 4-frame clip

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed,
                              /*dt=*/0.15f); // one whole frame elapses
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
}

TEST_CASE("clip: play_clip starts the clip's sheet on its first frame") {
  Fixture f;
  f.sprites.frame_index_ref(f.player) = 2;
  f.animations.timer_ref(f.player) = 0.05f;

  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_clip_sprite);
  CHECK_EQ(f.sprites.frame_index_ref(f.player), 0);
  CHECK_EQ(f.animations.timer_ref(f.player), 0.f);
  CHECK_EQ(f.animations.frame_count(f.player, corundum::sprites::AnimId::South), 3);
  CHECK_EQ(f.clips.resume_sprite_of(f.player), 0);
  CHECK_EQ(f.clips.resume_counts_of(f.player)[std::to_underlying(corundum::sprites::AnimId::South)], 4);
}

TEST_CASE("clip: play_clip faces the way the entity faces") {
  Fixture f;
  f.facings.insert(f.player, corundum::core::Direction::West);

  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  CHECK_EQ(f.sprites.anim_id_ref(f.player), corundum::sprites::AnimId::West);
}

TEST_CASE("clip: update leaves an entity playing a clip alone") {
  Fixture f;
  f.motion_sprites.insert(f.player, MotionSpriteTable::Config{
                                        .walk_id = corundum::sprites::SpriteId{9},
                                        .idle_id = corundum::sprites::SpriteId{0},
                                        .walk_counts = {},
                                        .idle_counts = {},
                                        .idle_to_walk_delay = 0.f,
                                        .walk_to_idle_delay = 0.f,
                                        .walk_frame_duration = 0.f,
                                        .idle_frame_duration = 0.f,
                                    });
  move(f, f.player);
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  corundum::animation::update(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, f.clips, k_iso,
                              k_reference_speed, k_clip_frame_seconds);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_clip_sprite);
  CHECK_EQ(f.sprites.frame_index_ref(f.player), 0);
}

TEST_CASE("clip: a clip advances at its own frame duration") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  run_clip_steps(f, 1);

  CHECK_EQ(f.sprites.frame_index_ref(f.player), 1);
}

TEST_CASE("clip: a Resume clip returns to its sheet after the last frame") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  run_clip_steps(f, 3);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), 0);
  CHECK_EQ(f.sprites.frame_index_ref(f.player), 0);
  CHECK_EQ(f.animations.frame_count(f.player, corundum::sprites::AnimId::South), 4);
  CHECK_FALSE(f.clips.has(f.player));
}

TEST_CASE("clip: a Resume clip ends when the entity moves") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));
  move(f, f.player);

  run_clip_steps(f, 1);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), 0);
  CHECK_FALSE(f.clips.has(f.player));
}

TEST_CASE("clip: a Hold clip stays on its last frame") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Hold));

  run_clip_steps(f, 5);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_clip_sprite);
  CHECK_EQ(f.sprites.frame_index_ref(f.player), 2);
  CHECK(f.clips.has_finished(f.player));
}

TEST_CASE("clip: a Hold clip ignores movement") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Hold));
  move(f, f.player);

  run_clip_steps(f, 1);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_clip_sprite);
  CHECK_EQ(f.sprites.frame_index_ref(f.player), 1);
}

TEST_CASE("clip: a clip started over another keeps the sheet to return to") {
  Fixture f;
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));

  play(f, f.player, make_clip(corundum::entities::ClipEnd::Hold, k_other_clip_sprite));

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_other_clip_sprite);
  CHECK_EQ(f.clips.resume_sprite_of(f.player), 0);
  CHECK_EQ(f.clips.end_of(f.player), corundum::entities::ClipEnd::Hold);
}

TEST_CASE("clip: update_clips with no clips does nothing") {
  Fixture f;

  run_clip_steps(f, 1);

  CHECK_EQ(f.sprites.frame_index_ref(f.player), 0);
  CHECK_FALSE(f.clips.has(f.player));
}

TEST_CASE("clip: one clip ending leaves the others playing") {
  Fixture f;
  const EntityId second = add_animated_entity(f, 1);
  const EntityId third = add_animated_entity(f, 2);
  const auto clip = make_clip(corundum::entities::ClipEnd::Resume);
  play(f, f.player, clip);
  play(f, second, clip);
  play(f, third, clip);
  move(f, f.player);

  run_clip_steps(f, 1);

  CHECK_FALSE(f.clips.has(f.player));
  REQUIRE(f.clips.has(second));
  REQUIRE(f.clips.has(third));
  CHECK_EQ(f.sprites.frame_index_ref(second), 1);
  CHECK_EQ(f.sprites.frame_index_ref(third), 1);
}

TEST_CASE("stance: a standing entity moves onto its ready sheet") {
  Fixture f;
  add_stance_row(f, f.player);

  f.motion_sprites.enter_stance(f.player);
  run_motion_step(f);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_stance_ready_sprite);
  CHECK_EQ(f.animations.frame_count(f.player, corundum::sprites::AnimId::South), 6);
  CHECK_EQ(f.motion_sprites.frame_duration_for(f.player, k_stance_ready_sprite), k_stance_ready_frame_seconds);
}

TEST_CASE("stance: leaving it returns a standing entity to its idle sheet") {
  Fixture f;
  add_stance_row(f, f.player);
  f.motion_sprites.enter_stance(f.player);
  run_motion_step(f);

  f.motion_sprites.leave_stance(f.player);
  run_motion_step(f);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_stance_idle_sprite);
  CHECK_EQ(f.animations.frame_count(f.player, corundum::sprites::AnimId::South), 4);
  CHECK_EQ(f.motion_sprites.frame_duration_for(f.player, k_stance_idle_sprite), k_stance_idle_frame_seconds);
}

TEST_CASE("stance: a moving entity still walks") {
  Fixture f;
  add_stance_row(f, f.player);
  f.motion_sprites.enter_stance(f.player);
  move(f, f.player);

  run_motion_step(f);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_stance_walk_sprite);
}

TEST_CASE("stance: a character with no ready sheet stays on idle") {
  Fixture f;
  f.motion_sprites.insert(f.player,
                          MotionSpriteTable::Config{.walk_id = k_stance_walk_sprite, .idle_id = k_stance_idle_sprite});

  f.motion_sprites.enter_stance(f.player);

  CHECK_FALSE(f.motion_sprites.is_in_stance(f.player));
  CHECK_EQ(f.motion_sprites.idle_sprite(f.player), k_stance_idle_sprite);
}

TEST_CASE("stance: removing another row keeps it") {
  Fixture f;
  const EntityId other = add_animated_entity(f, 1);
  add_stance_row(f, f.player);
  add_stance_row(f, other);
  f.motion_sprites.enter_stance(other);

  f.motion_sprites.remove(f.player);

  CHECK(f.motion_sprites.is_in_stance(other));
  CHECK_EQ(f.motion_sprites.idle_sprite(other), k_stance_ready_sprite);
}

TEST_CASE("stance: a clip that returns to the idle sheet moves on to the ready sheet") {
  Fixture f;
  add_stance_row(f, f.player);
  play(f, f.player, make_clip(corundum::entities::ClipEnd::Resume));
  f.motion_sprites.enter_stance(f.player);
  run_clip_steps(f, 3);
  REQUIRE_EQ(f.sprites.sprite_id_ref(f.player), k_stance_idle_sprite);

  run_motion_step(f);

  CHECK_EQ(f.sprites.sprite_id_ref(f.player), k_stance_ready_sprite);
}
