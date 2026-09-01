#include <doctest/doctest.h>

#include <corundum/anim/anim_sys.hpp>
#include <corundum/ecs/component/animation_table.hpp>
#include <corundum/ecs/component/facing_table.hpp>
#include <corundum/ecs/component/motion_sprite_table.hpp>
#include <corundum/ecs/component/sprite_table.hpp>
#include <corundum/ecs/component/transform_table.hpp>

#include <array>

using corundum::core::math::IsometricParams;
using corundum::ecs::AnimationTable;
using corundum::ecs::EntityId;
using corundum::ecs::FacingTable;
using corundum::ecs::MotionSpriteTable;
using corundum::ecs::SpriteTable;
using corundum::ecs::TransformTable;

namespace {

  // half_tw/half_th form a 3-4-5 right triangle so every screen-speed ratio exercised
  // below is exact in floating point (no ulp slop flipping an exact >= threshold check).
  constexpr IsometricParams k_iso{4.f, 3.f, 0.f, 0.f};
  constexpr float k_reference_speed = 10.f; // screen px/s

  struct Fixture {
    SpriteTable sprites;
    TransformTable transforms;
    AnimationTable animations;
    FacingTable facings;
    MotionSpriteTable motion_sprites;
    EntityId player = static_cast<EntityId>(0);

    Fixture() {
      transforms.insert(player, 0.5f, 0.5f, 0.f, 0.f);
      sprites.insert(player, corundum::resources::SpriteId{0}, corundum::resources::AnimId::South, 0);
      animations.insert(player);
      std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
      counts.fill(4); // every clip has frames, regardless of which facing gets resolved
      animations.set_frame_counts(player, counts);
      animations.frame_duration_ref(player) = 0.2f;
    }
  };

} // namespace

TEST_CASE("animate: moving at exactly reference speed advances at the authored frame_duration") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  // Pure +col move at exactly the reference screen speed: |screen_vel| = 2*hypot(4,3) = 10.
  f.transforms.dc[slot] = 2.f; // scale = 1
  f.transforms.dr[slot] = 0.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1); // one full frame_duration elapsed at scale 1
}

TEST_CASE("animate: moving at 2x reference speed reaches the frame threshold in half the dt") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 4.f; // |screen_vel| = 20 → scale = 2
  f.transforms.dr[slot] = 0.f;

  // dt=0.1s * scale=2 == 0.2s == frame_duration: crosses the threshold; at scale=1 it would not.
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.1f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: idle (zero velocity) still advances at the unscaled rate, not frozen") {
  Fixture f; // dc=dr=0 from the fixture — must not scale to 0
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: degenerate iso falls back to the unscaled rate (no divide-by-zero/NaN)") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 5.f; // nonzero velocity, but iso below is degenerate
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites,
                          IsometricParams{0.f, 0.f, 0.f, 0.f}, k_reference_speed, /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: pure +dc motion resolves facing to SouthEast (zone 1, col-dominant)") {
  Fixture f;
  f.facings.insert(f.player, corundum::ecs::FacingDir::South);
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::ecs::FacingDir::SouthEast);
}

TEST_CASE("animate: pure +dr motion resolves facing to SouthWest (zone 0, row-dominant)") {
  Fixture f;
  f.facings.insert(f.player, corundum::ecs::FacingDir::South);
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 0.f;
  f.transforms.dr[slot] = 1.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::ecs::FacingDir::SouthWest);
}

TEST_CASE("animate: equal-magnitude diagonal motion resolves facing to a screen-cardinal (zone 2)") {
  Fixture f;
  f.facings.insert(f.player, corundum::ecs::FacingDir::South);
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 1.f; // |dc| == |dr| → zone 2 (diagonal); +dr,+dc → screen South

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.facings.dir_of(f.player) == corundum::ecs::FacingDir::South);
}

TEST_CASE("animate: falls back to a cardinal AnimId when the directional clip is empty") {
  Fixture f;
  // Empty every clip except the East cardinal, so a diagonal SouthEast resolve
  // (from pure +dc motion) must walk back to its horizontal-fallback AnimId::East.
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::resources::AnimId::East)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::resources::AnimId::East);
}

TEST_CASE("animate: vertical-axis fallback is used when |dr| dominates") {
  Fixture f;
  // Empty every clip except the North cardinal. Pure +dr motion picks facing=SouthWest,
  // which is empty → falls back along the dominant vertical axis → AnimId::South.
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::resources::AnimId::South)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 0.f;
  f.transforms.dr[slot] = 1.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::resources::AnimId::South);
}

TEST_CASE("animate: returns AnimId::Default when every directional and cardinal clip is empty") {
  Fixture f;
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{}; // all zero
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  f.transforms.dr[slot] = 0.f;

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::resources::AnimId::Default);
}

TEST_CASE("animate: idle entity with no FacingTable uses the South default for AnimId lookup") {
  // Fixture has no FacingTable entry — `facings.has(e)` is false, so the resolve
  // falls back to FacingDir::South while idle. Without the default, accessing
  // `facings.dir_of(e)` would assert.
  Fixture f; // no facings.insert; counts.fill(4) leaves every clip available.
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::resources::AnimId::South);
}

TEST_CASE("animate: changing AnimId resets frame_index and timer") {
  Fixture f;
  // Build up a non-zero frame state.
  f.animations.frame_duration_ref(f.player) = 0.1f;
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.5f);
  REQUIRE(f.sprites.frame_index_ref(f.player) != 0);

  // Now restrict counts to AnimId::East only and start moving → South→East transition
  // must zero frame_index and timer.
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts[static_cast<uint8_t>(corundum::resources::AnimId::East)] = 4;
  f.animations.set_frame_counts(f.player, counts);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);
  CHECK(f.sprites.anim_id_ref(f.player) == corundum::resources::AnimId::East);
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
}

TEST_CASE("animate: single-frame AnimIds do not advance or accumulate timer") {
  Fixture f;
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts.fill(1); // every clip is a static image
  f.animations.set_frame_counts(f.player, counts);

  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/10.f); // huge dt
  CHECK(f.sprites.frame_index_ref(f.player) == 0);
}

TEST_CASE("animate: motion sprite commits after the configured idle-to-walk delay") {
  Fixture f;
  constexpr corundum::resources::SpriteId walk_sid{1};
  constexpr corundum::resources::SpriteId idle_sid{2};
  // Start with the idle sprite active so the transition is observable from the
  // initial state (the fixture otherwise initialises sprite_id to 0).
  f.sprites.sprite_id_ref(f.player) = idle_sid;
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts.fill(4);
  f.motion_sprites.insert(f.player, walk_sid, idle_sid, counts, counts,
                          /*itw=*/0.1f, /*wti=*/0.f);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;

  // Half the delay — not yet committed; pending is set.
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.05f);
  CHECK(f.sprites.sprite_id_ref(f.player) == idle_sid);
  CHECK(f.motion_sprites.pending_sprite(f.player) == walk_sid);

  // Cumulative dt reaches the delay — commits.
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.05f);
  CHECK(f.sprites.sprite_id_ref(f.player) == walk_sid);
  CHECK(f.motion_sprites.pending_sprite(f.player) == corundum::resources::k_null_sprite_id);
}

TEST_CASE("animate: motion sprite cancels a pending transition when motion state flips before delay") {
  Fixture f;
  constexpr corundum::resources::SpriteId walk_sid{1};
  constexpr corundum::resources::SpriteId idle_sid{2};
  f.sprites.sprite_id_ref(f.player) = idle_sid;
  std::array<uint8_t, corundum::resources::k_num_anim_ids> counts{};
  counts.fill(4);
  f.motion_sprites.insert(f.player, walk_sid, idle_sid, counts, counts,
                          /*itw=*/0.1f, /*wti=*/0.f);

  const auto slot = f.transforms.dense_idx(f.player);

  // Start moving, partial-transition the timer toward the idle→walk commit.
  f.transforms.dc[slot] = 1.f;
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.05f);
  REQUIRE(f.motion_sprites.pending_sprite(f.player) == walk_sid);

  // Stop moving — desired flips back to idle, which is already cur_sid → cancel.
  f.transforms.dc[slot] = 0.f;
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.05f);
  CHECK(f.motion_sprites.pending_sprite(f.player) == corundum::resources::k_null_sprite_id);
  CHECK(f.sprites.sprite_id_ref(f.player) == idle_sid);
}

TEST_CASE("animate: motion sprite commit refreshes AnimationTable frame counts and duration") {
  Fixture f;
  constexpr corundum::resources::SpriteId walk_sid{1};
  constexpr corundum::resources::SpriteId idle_sid{2};
  std::array<uint8_t, corundum::resources::k_num_anim_ids> walk_counts{};
  walk_counts.fill(6);
  walk_counts[static_cast<uint8_t>(corundum::resources::AnimId::South)] = 8; // distinguish from idle
  std::array<uint8_t, corundum::resources::k_num_anim_ids> idle_counts{};
  idle_counts.fill(4);
  f.motion_sprites.insert(f.player, walk_sid, idle_sid, walk_counts, idle_counts,
                          /*itw=*/0.f, /*wti=*/0.f, /*walk_fd=*/0.05f, /*idle_fd=*/0.25f);

  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 1.f;
  corundum::anim::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso, k_reference_speed,
                          /*dt=*/0.f);

  CHECK(f.sprites.sprite_id_ref(f.player) == walk_sid);
  CHECK(f.animations.frame_count(f.player, corundum::resources::AnimId::South) == 8);
  CHECK(f.animations.frame_duration_ref(f.player) == doctest::Approx(0.05f));
}
