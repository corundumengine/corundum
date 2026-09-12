#include <doctest/doctest.h>

#include <corundum/core/direction.hpp>
#include <corundum/sprites/sprite.hpp>

#include <cstdint>
#include <utility>

using corundum::core::Direction;

TEST_CASE("direction: opposite is an involution over every direction") {
  for (uint8_t i = 0; i < corundum::core::k_num_directions; ++i) {
    const auto dir = static_cast<Direction>(i);
    CHECK(corundum::core::opposite(corundum::core::opposite(dir)) == dir);
  }
}

TEST_CASE("direction: names round-trip through direction_from_name") {
  for (uint8_t i = 0; i < corundum::core::k_num_directions; ++i) {
    const auto dir = static_cast<Direction>(i);
    CHECK(corundum::core::direction_from_name(corundum::core::direction_name(dir)) == dir);
  }
}

TEST_CASE("direction: unknown and clip-only names resolve to nullopt") {
  CHECK_FALSE(corundum::core::direction_from_name("sideways").has_value());
  CHECK_FALSE(corundum::core::direction_from_name("default").has_value());
}

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
