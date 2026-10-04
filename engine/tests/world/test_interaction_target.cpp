// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/update.hpp>

#include <cstddef>
#include <optional>

namespace {

  using corundum::entities::Animation;
  using corundum::entities::DialogueRef;
  using corundum::entities::EntityId;
  using corundum::entities::Position;
  using corundum::entities::Sprite;
  using corundum::entities::Velocity;
  using corundum::input::Action;
  using corundum::sprites::AnimId;
  using corundum::sprites::k_null_sprite_id;

  /// A scene with an animated player at (5,5), a flat map, and an interactable NPC at
  /// (@p npc_col, @p npc_row) unless @p npc_present is false.
  struct Fixture {
    corundum::world::Scene scene;
    corundum::world::MapView map;
    corundum::core::GameConfig cfg;
    EntityId player{};
    EntityId npc{};
  };

  Fixture make_fixture(float npc_col = 5.f, float npc_row = 6.f, bool npc_present = true) {
    const Sprite sprite{.sprite_id = k_null_sprite_id, .anim_id = AnimId::Default, .frame_index = 0};
    const Velocity velocity{};
    const Animation animation{};
    const Position player_pos{.col = 5.f, .row = 5.f};

    Fixture f;
    const std::optional<EntityId> player =
        corundum::entities::spawn(f.scene.world, player_pos, velocity, sprite, animation);
    REQUIRE(player.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above guards the deref.
    f.player = *player;
    f.scene.player = f.player;
    f.scene.world.collisions.insert(f.player, 1.f, 1.f);

    if (npc_present) {
      const Position npc_pos{.col = npc_col, .row = npc_row};
      const std::optional<EntityId> npc =
          corundum::entities::spawn(f.scene.world, npc_pos, velocity, sprite, DialogueRef{.graph_id = "npc"});
      REQUIRE(npc.has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above guards the deref.
      f.npc = *npc;
    }

    f.map.half_tw = 64.f;
    f.map.half_th = 32.f;
    f.map.world_w_tiles = 100.f;
    f.map.world_h_tiles = 100.f;
    return f;
  }

  void step(Fixture &f, const corundum::input::InputState &input) {
    corundum::world::update(f.scene, f.cfg, input, f.map, 1.f / 60.f, 320.f, 240.f);
  }

} // namespace

TEST_CASE("interaction target: an activate press targets the nearby NPC") {
  Fixture f = make_fixture();
  corundum::input::InputState input{};
  input.pressed.set(static_cast<std::size_t>(Action::Activate));

  step(f, input);

  REQUIRE(f.scene.pending_interaction.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above guards the deref.
  CHECK(*f.scene.pending_interaction == f.npc);
}

TEST_CASE("interaction target: no activate press leaves it unset") {
  Fixture f = make_fixture();

  step(f, corundum::input::InputState{});

  CHECK_FALSE(f.scene.pending_interaction.has_value());
}

TEST_CASE("interaction target: an out-of-range NPC is not targeted") {
  Fixture f = make_fixture(/*npc_col=*/5.f, /*npc_row=*/9.f);
  corundum::input::InputState input{};
  input.pressed.set(static_cast<std::size_t>(Action::Activate));

  step(f, input);

  CHECK_FALSE(f.scene.pending_interaction.has_value());
}

TEST_CASE("interaction target: an entity without a dialogue ref is not targeted") {
  Fixture f = make_fixture(/*npc_col=*/5.f, /*npc_row=*/6.f, /*npc_present=*/false);
  const Sprite sprite{.sprite_id = k_null_sprite_id, .anim_id = AnimId::Default, .frame_index = 0};
  const Velocity velocity{};
  const Animation animation{};
  const Position bystander_pos{.col = 5.f, .row = 6.f};
  const std::optional<EntityId> bystander =
      corundum::entities::spawn(f.scene.world, bystander_pos, velocity, sprite, animation);
  REQUIRE(bystander.has_value());

  corundum::input::InputState input{};
  input.pressed.set(static_cast<std::size_t>(Action::Activate));
  step(f, input);

  CHECK_FALSE(f.scene.pending_interaction.has_value());
}
