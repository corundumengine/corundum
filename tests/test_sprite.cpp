#include <doctest/doctest.h>

#include <corundum/resources/sprite.hpp>

namespace res = corundum::resources;

TEST_CASE("frame_origin: zero offset and spacing") {
  const res::IntPoint p = res::frame_origin(0, 0, 16, 16, 0, 0, 2, 3);
  CHECK(p.x == 32);
  CHECK(p.y == 48);
}

TEST_CASE("frame_origin: non-zero offset") {
  const res::IntPoint p = res::frame_origin(5, 10, 16, 16, 0, 0, 0, 0);
  CHECK(p.x == 5);
  CHECK(p.y == 10);
}

TEST_CASE("frame_origin: non-zero spacing") {
  const res::IntPoint p = res::frame_origin(0, 0, 16, 16, 2, 2, 1, 0);
  CHECK(p.x == 18);
  CHECK(p.y == 0);
}

TEST_CASE("frame_origin: from SpriteSheet") {
  res::SpriteSheet sheet;
  sheet.offset_x = 10;
  sheet.offset_y = 20;
  sheet.frame_width = 32;
  sheet.frame_height = 48;
  sheet.spacing_x = 2;
  sheet.spacing_y = 4;
  const res::IntPoint p = res::frame_origin(sheet, res::FrameCoord{3, 1});
  CHECK(p.x == 10 + 3 * (32 + 2));
  CHECK(p.y == 20 + 1 * (48 + 4));
}

TEST_CASE("frame_origin: col=0 row=0") {
  const res::IntPoint p = res::frame_origin(10, 20, 32, 48, 4, 6, 0, 0);
  CHECK(p.x == 10);
  CHECK(p.y == 20);
}
