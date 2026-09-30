// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/direction.hpp>
#include <corundum/sprites/sprite.hpp>

#include <utility>

using corundum::core::Direction;

TEST_CASE("sprites: AnimId directional values mirror Direction with Default appended") {
  CHECK(corundum::sprites::to_anim(Direction::South) == corundum::sprites::AnimId::South);
  CHECK(corundum::sprites::to_anim(Direction::NorthWest) == corundum::sprites::AnimId::NorthWest);
  CHECK(std::to_underlying(corundum::sprites::AnimId::Default) == corundum::core::k_num_directions);
  CHECK(corundum::sprites::k_anim_names[std::to_underlying(corundum::sprites::AnimId::South)] == "south");
}

TEST_CASE("sprites: anim_name_to_id resolves directions, default, and rejects unknown") {
  CHECK(corundum::sprites::anim_name_to_id("south") == corundum::sprites::AnimId::South);
  CHECK(corundum::sprites::anim_name_to_id("default") == corundum::sprites::AnimId::Default);
  CHECK(corundum::sprites::anim_name_to_id("sideways") == corundum::sprites::AnimId::Count);
}
