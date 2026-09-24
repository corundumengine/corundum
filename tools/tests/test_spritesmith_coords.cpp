// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "../editors/spritesmith/coords.hpp"

#include <corundum/sprites/sprite.hpp>
#include <corundum/sprites/sprite_atlas.hpp>
#include <optional>
#include <utility>
#include <vector>

using corundum::sprites::AtlasSprite;
using corundum::sprites::FrameCoord;
using tools::spritesmith::clamp_camera;
using tools::spritesmith::frame_to_canvas_rect;
using tools::spritesmith::FrameRect;
using tools::spritesmith::screen_to_frame;
using tools::spritesmith::sprite_at_point;

namespace {

  AtlasSprite make_sprite(int x, int y, int w, int h) {
    AtlasSprite sprite;
    sprite.x = x;
    sprite.y = y;
    sprite.w = w;
    sprite.h = h;
    return sprite;
  }

  // A 4x4 grid of 16x16 frames, no offsets or spacing, unit zoom, no camera.
  [[nodiscard]] std::optional<FrameCoord> frame_4x4(int px, int py) {
    return screen_to_frame(px, py, /*canvas_w=*/100, /*canvas_h=*/100, /*camera_x=*/0.f, /*camera_y=*/0.f,
                           /*zoom=*/1.f, /*frame_w=*/16, /*frame_h=*/16, /*offset_x=*/0, /*offset_y=*/0,
                           /*spacing_x=*/0, /*spacing_y=*/0, /*img_cols=*/4, /*img_rows=*/4);
  }

} // namespace

TEST_CASE("screen_to_frame — maps canvas pixels to grid cells") {
  const auto origin = frame_4x4(0, 0);
  REQUIRE(origin.has_value());
  CHECK(origin->col == 0);
  CHECK(origin->row == 0);

  const auto top_right = frame_4x4(16, 0);
  REQUIRE(top_right.has_value());
  CHECK(top_right->col == 1);
  CHECK(top_right->row == 0);

  const auto last_cell = frame_4x4(63, 63);
  REQUIRE(last_cell.has_value());
  CHECK(last_cell->col == 3);
  CHECK(last_cell->row == 3);
}

TEST_CASE("screen_to_frame — rejects positions outside the canvas") {
  CHECK_FALSE(screen_to_frame(-1, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 4, 4).has_value());
  CHECK_FALSE(screen_to_frame(0, -1, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 4, 4).has_value());
  CHECK_FALSE(screen_to_frame(100, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 4, 4).has_value());
  CHECK_FALSE(screen_to_frame(0, 100, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 4, 4).has_value());
}

TEST_CASE("screen_to_frame — rejects degenerate frame/grid dimensions") {
  CHECK_FALSE(screen_to_frame(0, 0, 100, 100, 0.f, 0.f, 1.f, 0, 16, 0, 0, 0, 0, 4, 4).has_value());
  CHECK_FALSE(screen_to_frame(0, 0, 100, 100, 0.f, 0.f, 1.f, 16, 0, 0, 0, 0, 0, 4, 4).has_value());
  CHECK_FALSE(screen_to_frame(0, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 0, 4).has_value());
  CHECK_FALSE(screen_to_frame(0, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, 0, 0, 4, 0).has_value());
}

TEST_CASE("screen_to_frame — rejects clicks inside the inter-frame spacing gap") {
  // 16px frames with a 4px horizontal gap: cell width is 20.
  const auto hit = [](int px) {
    return screen_to_frame(px, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, 0, 0, /*spacing_x=*/4, /*spacing_y=*/0, 4, 4);
  };

  CHECK(hit(15).has_value());       // last pixel of frame 0
  CHECK_FALSE(hit(16).has_value()); // first gap pixel
  CHECK_FALSE(hit(19).has_value()); // last gap pixel
  REQUIRE(hit(20).has_value());     // first pixel of frame 1
  CHECK(hit(20)->col == 1);
}

TEST_CASE("screen_to_frame — applies the image offset") {
  const auto hit = [](int px) {
    return screen_to_frame(px, 0, 100, 100, 0.f, 0.f, 1.f, 16, 16, /*offset_x=*/8, /*offset_y=*/0, 0, 0, 4, 4);
  };

  CHECK_FALSE(hit(7).has_value()); // before the first frame
  const auto first = hit(8);
  REQUIRE(first.has_value());
  CHECK(first->col == 0);
}

TEST_CASE("screen_to_frame — applies camera offset and zoom") {
  // zoom 2 + camera 16 maps canvas x=0 to image x=8, i.e. cell 1 of an 8px grid.
  const auto cell = screen_to_frame(/*px=*/0, /*py=*/0, /*canvas_w=*/100, /*canvas_h=*/100, /*camera_x=*/16.f,
                                    /*camera_y=*/16.f, /*zoom=*/2.f, /*frame_w=*/8, /*frame_h=*/8, /*offset_x=*/0,
                                    /*offset_y=*/0, /*spacing_x=*/0, /*spacing_y=*/0, /*img_cols=*/4, /*img_rows=*/4);
  REQUIRE(cell.has_value());
  CHECK(cell->col == 1);
  CHECK(cell->row == 1);
}

TEST_CASE("screen_to_frame — rejects cells past the authored grid bounds") {
  // Clicking cell column 2 when the sheet only has 2 columns must miss.
  const auto cell = screen_to_frame(/*px=*/32, /*py=*/0, /*canvas_w=*/100, /*canvas_h=*/100, /*camera_x=*/0.f,
                                    /*camera_y=*/0.f, /*zoom=*/1.f, /*frame_w=*/16, /*frame_h=*/16, /*offset_x=*/0,
                                    /*offset_y=*/0, /*spacing_x=*/0, /*spacing_y=*/0, /*img_cols=*/2, /*img_rows=*/2);
  CHECK_FALSE(cell.has_value());
}

TEST_CASE("frame_to_canvas_rect — positions and scales a frame cell") {
  // origin = (4 + 1*(16+2), 4 + 2*(16+2)) = (22, 40); screen = origin*2 - camera.
  const FrameRect rect = frame_to_canvas_rect(/*col=*/1, /*row=*/2, /*camera_x=*/10.f, /*camera_y=*/20.f,
                                              /*zoom=*/2.f, /*frame_w=*/16, /*frame_h=*/16, /*offset_x=*/4,
                                              /*offset_y=*/4, /*spacing_x=*/2, /*spacing_y=*/2);
  CHECK(rect.x == doctest::Approx(34.f));
  CHECK(rect.y == doctest::Approx(60.f));
  CHECK(rect.w == doctest::Approx(32.f));
  CHECK(rect.h == doctest::Approx(32.f));
}

TEST_CASE("sprite_at_point — hit-tests sprites in image space") {
  const std::vector<AtlasSprite> sprites = {make_sprite(0, 0, 10, 10), make_sprite(20, 20, 10, 10)};

  CHECK(sprite_at_point(5, 5, 100, 100, 0.f, 0.f, 1.f, sprites) == 0);
  CHECK(sprite_at_point(25, 25, 100, 100, 0.f, 0.f, 1.f, sprites) == 1);
  CHECK(sprite_at_point(15, 15, 100, 100, 0.f, 0.f, 1.f, sprites) == -1);
  // The right/bottom edges are exclusive.
  CHECK(sprite_at_point(10, 5, 100, 100, 0.f, 0.f, 1.f, sprites) == -1);
}

TEST_CASE("sprite_at_point — rejects out-of-canvas positions and non-positive zoom") {
  const std::vector<AtlasSprite> sprites = {make_sprite(0, 0, 10, 10)};

  CHECK(sprite_at_point(-1, 0, 100, 100, 0.f, 0.f, 1.f, sprites) == -1);
  CHECK(sprite_at_point(100, 0, 100, 100, 0.f, 0.f, 1.f, sprites) == -1);
  CHECK(sprite_at_point(0, 100, 100, 100, 0.f, 0.f, 1.f, sprites) == -1);
  CHECK(sprite_at_point(0, 0, 100, 100, 0.f, 0.f, 0.f, sprites) == -1);
}

TEST_CASE("sprite_at_point — respects camera offset and zoom") {
  const std::vector<AtlasSprite> sprites = {make_sprite(0, 0, 10, 10)};

  // canvas 0 + camera 5, zoom 2 => image x 2.5, inside sprite 0.
  CHECK(sprite_at_point(0, 0, 100, 100, 5.f, 5.f, 2.f, sprites) == 0);
}

TEST_CASE("clamp_camera — an image smaller than the canvas pins the camera to the origin") {
  const auto [x, y] = clamp_camera(/*cx=*/50.f, /*cy=*/50.f, /*zoom=*/1.f, /*img_w=*/100, /*img_h=*/100,
                                   /*canvas_w=*/200, /*canvas_h=*/200);
  CHECK(x == doctest::Approx(0.f));
  CHECK(y == doctest::Approx(0.f));
}

TEST_CASE("clamp_camera — clamps to both ends of the scrollable range") {
  // img 400x300, canvas 200x200, zoom 1 => x in [0, 200], y in [0, 100].
  CHECK(clamp_camera(250.f, 150.f, 1.f, 400, 300, 200, 200) == std::pair{200.f, 100.f});
  CHECK(clamp_camera(-5.f, -5.f, 1.f, 400, 300, 200, 200) == std::pair{0.f, 0.f});
  CHECK(clamp_camera(50.f, 50.f, 1.f, 400, 300, 200, 200) == std::pair{50.f, 50.f});
}

TEST_CASE("clamp_camera — zoom scales the scrollable range") {
  // Same 400x300 image at zoom 2 => 800x600, so x in [0, 600], y in [0, 400].
  CHECK(clamp_camera(9999.f, 9999.f, 2.f, 400, 300, 200, 200) == std::pair{600.f, 400.f});
}
