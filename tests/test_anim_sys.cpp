#include <doctest/doctest.h>

#include <corundum/anim/sys/anim_sys.hpp>
#include <corundum/gameplay/component/animation_table.hpp>
#include <corundum/gameplay/component/facing_table.hpp>
#include <corundum/gameplay/component/motion_sprite_table.hpp>
#include <corundum/gameplay/component/sprite_table.hpp>
#include <corundum/gameplay/component/transform_table.hpp>

#include <array>

using corundum::core::math::IsometricParams;
using corundum::gameplay::component::AnimationTable;
using corundum::gameplay::component::FacingTable;
using corundum::gameplay::component::MotionSpriteTable;
using corundum::gameplay::component::SpriteTable;
using corundum::gameplay::component::TransformTable;
using corundum::gameplay::entity::EntityId;

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

  corundum::anim::sys::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso,
                               k_reference_speed, /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1); // one full frame_duration elapsed at scale 1
}

TEST_CASE("animate: moving at 2x reference speed reaches the frame threshold in half the dt") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 4.f; // |screen_vel| = 20 → scale = 2
  f.transforms.dr[slot] = 0.f;

  // dt=0.1s * scale=2 == 0.2s == frame_duration: crosses the threshold; at scale=1 it would not.
  corundum::anim::sys::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso,
                               k_reference_speed, /*dt=*/0.1f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: idle (zero velocity) still advances at the unscaled rate, not frozen") {
  Fixture f; // dc=dr=0 from the fixture — must not scale to 0
  corundum::anim::sys::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites, k_iso,
                               k_reference_speed, /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}

TEST_CASE("animate: degenerate iso falls back to the unscaled rate (no divide-by-zero/NaN)") {
  Fixture f;
  const auto slot = f.transforms.dense_idx(f.player);
  f.transforms.dc[slot] = 5.f; // nonzero velocity, but iso below is degenerate
  corundum::anim::sys::animate(f.sprites, f.transforms, f.animations, f.facings, f.motion_sprites,
                               IsometricParams{0.f, 0.f, 0.f, 0.f}, k_reference_speed, /*dt=*/0.2f);
  CHECK(f.sprites.frame_index_ref(f.player) == 1);
}
