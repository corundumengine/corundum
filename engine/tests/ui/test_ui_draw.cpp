// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <cstddef>
#include <doctest/doctest.h>

#include <array>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/prompt_box.hpp>
#include <corundum/ui/ui_draw.hpp>

#include "ui/recording_renderer.hpp"

#include <string_view>
#include <variant>

using corundum::platform::DrawRect;
using corundum::platform::DrawSprite;
using corundum::platform::DrawText;
using corundum::test::make_border;
using corundum::test::RecordingRenderer;

// doctest's REQUIRE/CHECK macros expand to control flow, so the assertion count — not the
// test's logic — dominates this metric.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("ui_draw: panel_chrome emits exactly one DrawRect then the border's sprite commands") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::core::math::Colour bg{.r = 20, .g = 20, .b = 20, .a = 200};
  const corundum::core::math::Vec2 pos{.x = 10.f, .y = 20.f};
  const corundum::core::math::Vec2 size{.x = 100.f, .y = 60.f};

  corundum::ui::panel_chrome(r, bg, border, pos, size);

  // The border draws 8 sprites (4 corners + 2 horizontal edges + 2 vertical edges).
  REQUIRE(r.log.size() == 9);

  // First call is the fill rect, then the border's sprites — render order matters
  // because the fill must be behind the frame.
  const DrawRect &first = std::get<DrawRect>(r.log[0]);
  CHECK(first.position.x == pos.x);
  CHECK(first.position.y == pos.y);
  CHECK(first.size.x == size.x);
  CHECK(first.size.y == size.y);
  CHECK(first.colour.r == bg.r);
  CHECK(first.colour.a == bg.a);

  for (std::size_t i = 1; i < r.log.size(); ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));
}

TEST_CASE("ui_draw: panel_chrome is a no-op for the sprite half when the border has no texture") {
  RecordingRenderer r;
  corundum::ui::NinePatchBorder border{};
  border.texture_id = 0;
  border.tile_w = 4;
  border.tile_h = 4;

  corundum::ui::panel_chrome(r, {.r = 0, .g = 0, .b = 0, .a = 255}, border, {.x = 0.f, .y = 0.f},
                             {.x = 50.f, .y = 50.f});

  // The fill still goes out; only the border is skipped.
  REQUIRE(r.log.size() == 1);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
}

TEST_CASE("ui_draw: panel_fill emits exactly one DrawRect") {
  RecordingRenderer r;
  const corundum::core::math::Colour bg{.r = 20, .g = 20, .b = 20, .a = 200};
  const corundum::core::math::Vec2 pos{.x = 10.f, .y = 20.f};
  const corundum::core::math::Vec2 size{.x = 100.f, .y = 60.f};

  corundum::ui::panel_fill(r, bg, pos, size);

  REQUIRE(r.log.size() == 1);
  const DrawRect &rect = std::get<DrawRect>(r.log[0]);
  CHECK(rect.position.x == pos.x);
  CHECK(rect.position.y == pos.y);
  CHECK(rect.size.x == size.x);
  CHECK(rect.size.y == size.y);
  CHECK(rect.colour.r == bg.r);
  CHECK(rect.colour.a == bg.a);
}

TEST_CASE("ui_draw: panel_frame emits only the border's sprites, with no fill") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();

  corundum::ui::panel_frame(r, border, {.x = 10.f, .y = 20.f}, {.x = 100.f, .y = 60.f});

  REQUIRE(r.log.size() == 8);
  for (const auto &call : r.log)
    CHECK(std::holds_alternative<DrawSprite>(call));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("ui_draw: nine_patch_render emits corners and correctly stretched edges") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border(); // 4×4 cells
  const corundum::core::math::Vec2 pos{.x = 10.f, .y = 20.f};
  const corundum::core::math::Vec2 size{.x = 100.f, .y = 60.f};

  corundum::ui::nine_patch_render(r, border, pos.x, pos.y, size.x, size.y);

  // Order: four corners at natural size (TL, TR, BL, BR), then horizontal edges, then vertical edges.
  REQUIRE(r.log.size() == 8);
  for (const auto &call : r.log)
    REQUIRE(std::holds_alternative<DrawSprite>(call));

  const auto sprite = [&](std::size_t i) -> const DrawSprite & { return std::get<DrawSprite>(r.log[i]); };

  // Top-left corner sits at the rect origin at natural size.
  CHECK(sprite(0).source.x == 0);
  CHECK(sprite(0).source.y == 0);
  CHECK(sprite(0).source.width == 4);
  CHECK(sprite(0).source.height == 4);
  CHECK(sprite(0).position.x == pos.x);
  CHECK(sprite(0).position.y == pos.y);
  CHECK(sprite(0).scale.x == 1.f);
  CHECK(sprite(0).scale.y == 1.f);

  // Top-right corner is pinned to the rect's right/top edges.
  CHECK(sprite(1).source.x == 8);
  CHECK(sprite(1).source.y == 0);
  CHECK(sprite(1).position.x == pos.x + size.x - 4.f);
  CHECK(sprite(1).position.y == pos.y);

  // Top-middle edge stretches only on X across the inner span: (100 - 8) / 4 = 23.
  CHECK(sprite(4).source.x == 4);
  CHECK(sprite(4).source.y == 0);
  CHECK(sprite(4).position.x == pos.x + 4.f);
  CHECK(sprite(4).position.y == pos.y);
  CHECK(sprite(4).scale.x == 23.f);
  CHECK(sprite(4).scale.y == 1.f);

  // Left-middle edge stretches only on Y: (60 - 8) / 4 = 13.
  CHECK(sprite(6).source.x == 0);
  CHECK(sprite(6).source.y == 4);
  CHECK(sprite(6).position.x == pos.x);
  CHECK(sprite(6).position.y == pos.y + 4.f);
  CHECK(sprite(6).scale.x == 1.f);
  CHECK(sprite(6).scale.y == 13.f);
}

TEST_CASE("ui_draw: nine_patch_render is a no-op without a texture or with non-positive cell size") {
  RecordingRenderer r;
  corundum::ui::NinePatchBorder no_texture{};
  no_texture.tile_w = 4;
  no_texture.tile_h = 4;
  corundum::ui::nine_patch_render(r, no_texture, 0.f, 0.f, 100.f, 60.f);

  corundum::ui::NinePatchBorder zero_cell{};
  zero_cell.texture_id = 1u;
  corundum::ui::nine_patch_render(r, zero_cell, 0.f, 0.f, 100.f, 60.f);

  CHECK(r.log.empty());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("ui_draw: nine_patch_render clamps a sub-cell rect instead of emitting negative scales") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border(); // 4×4 cells

  // 4×4 is narrower/shorter than two 4px corners, so both inner spans clamp to 0.
  corundum::ui::nine_patch_render(r, border, 0.f, 0.f, 4.f, 4.f);

  REQUIRE(r.log.size() == 8);
  for (const auto &call : r.log) {
    const auto &s = std::get<DrawSprite>(call);
    CHECK(s.scale.x >= 0.f);
    CHECK(s.scale.y >= 0.f);
  }
  CHECK(std::get<DrawSprite>(r.log[4]).scale.x == 0.f); // top-middle edge
  CHECK(std::get<DrawSprite>(r.log[6]).scale.y == 0.f); // left-middle edge
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with selected colours when selected=true") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  // style.selected defaults to (255, 255, 0, 255); style.choice defaults to (200, 200, 200, 255).

  const float cursor_w = corundum::ui::cursor_advance(r, style);
  corundum::ui::draw_option(r, style, "Hello", {.x = 5.f, .y = 7.f}, true);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // First draw: cursor prefix "> ".
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  CHECK(cursor.text == "> ");
  CHECK(cursor.position.x == 5.f);
  CHECK(cursor.position.y == 7.f);
  CHECK(cursor.colour.r == style.selected.r);
  CHECK(cursor.colour.g == style.selected.g);
  CHECK(cursor.colour.b == style.selected.b);
  // Second draw: label at pos.x + cursor_w, same colour as cursor.
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(label.text == "Hello");
  CHECK(label.position.x == 5.f + cursor_w);
  CHECK(label.position.y == 7.f);
  CHECK(label.colour.r == style.selected.r);

  CHECK(cursor_w > 0.f);
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with choice colours and two-space cursor when selected=false") {
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};

  const float cursor_w = corundum::ui::cursor_advance(r, style);
  corundum::ui::draw_option(r, style, "World", {.x = 0.f, .y = 0.f}, false);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // Cursor is the two-space placeholder; label follows at +cursor_w.
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(cursor.text == "  ");
  CHECK(label.text == "World");
  CHECK(label.position.x == cursor_w);
  // Both writes share the unselected colour (style.choice).
  CHECK(cursor.colour.r == style.choice.r);
  CHECK(cursor.colour.g == style.choice.g);
  CHECK(cursor.colour.b == style.choice.b);
  CHECK(label.colour.r == style.choice.r);
  CHECK(label.colour.g == style.choice.g);
  CHECK(label.colour.b == style.choice.b);
}

TEST_CASE("ui_draw: draw_option aligns the label at cursor_advance whether or not it is selected") {
  // Column alignment guarantee: selected and unselected options reserve the same cursor
  // column, so the label x is pos.x + cursor_advance in both branches. If a font rendered
  // "> " and "  " at different widths this would surface here — cursor_advance always
  // measures k_choice_cursor, which is the point.
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const float advance = corundum::ui::cursor_advance(r, style);

  corundum::ui::draw_option(r, style, "A", {.x = 0.f, .y = 0.f}, true);
  corundum::ui::draw_option(r, style, "A", {.x = 100.f, .y = 0.f}, false);

  REQUIRE(r.log.size() == 4);
  const DrawText &selected_label = std::get<DrawText>(r.log[1]);
  const DrawText &unselected_label = std::get<DrawText>(r.log[3]);
  CHECK(selected_label.position.x == advance);
  CHECK(unselected_label.position.x == 100.f + advance);
  CHECK(advance > 0.f);
}

TEST_CASE("ui_draw: draw_option without a cursor keeps the selected colour and the hanging indent") {
  // Continuation lines of a wrapped selected option: the whole option stays highlighted
  // (style.selected) even though only the first line shows the "> " cursor.
  RecordingRenderer r;
  const corundum::ui::PanelStyle style{};
  const float advance = corundum::ui::cursor_advance(r, style);

  corundum::ui::draw_option(r, style, "continuation", {.x = 3.f, .y = 9.f}, true, false);

  REQUIRE(r.log.size() == 2);
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(cursor.text == "  ");
  CHECK(label.text == "continuation");
  CHECK(label.position.x == 3.f + advance);
  CHECK(cursor.colour.r == style.selected.r);
  CHECK(label.colour.r == style.selected.r);
}

// ── prompt_box_render ───────────────────────────────────────────────────────

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("prompt_box_render: chrome, question, then the Yes/No option pairs") {
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);

  // Chrome (1 DrawRect + 8 DrawSprite), question, then Yes (cursor+label) and No (cursor+label).
  REQUIRE(r.log.size() == 9 + 1 + 4);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
  for (std::size_t i = 1; i < 9; ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));

  const DrawText &question = std::get<DrawText>(r.log[9]);
  CHECK(question.text == "Enter?");
  CHECK(question.colour.r == style.body.r);

  const DrawText &yes_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &yes_label = std::get<DrawText>(r.log[11]);
  const DrawText &no_cursor = std::get<DrawText>(r.log[12]);
  const DrawText &no_label = std::get<DrawText>(r.log[13]);

  CHECK(yes_cursor.text == "> ");
  CHECK(yes_label.text == "Yes");
  CHECK(yes_cursor.colour.r == style.selected.r);
  CHECK(no_cursor.text == "  ");
  CHECK(no_label.text == "No");
  CHECK(no_cursor.colour.r == style.choice.r);
  CHECK(no_label.position.y == yes_label.position.y); // both options share the row
  CHECK(no_label.position.x > yes_label.position.x);
}

TEST_CASE("prompt_box_render: the option row is centered within the panel") {
  // Regression: the row's width must count the cursor column ahead of both labels, or the
  // row is placed half a cursor-width right of center.
  RecordingRenderer r;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);

  const DrawRect &panel = std::get<DrawRect>(r.log[0]);
  const DrawText &yes_cursor = std::get<DrawText>(r.log[10]);
  const DrawText &no_label = std::get<DrawText>(r.log[13]);

  const float no_w = r.measure_text(style.font_id, "No", style.font_size_body);
  const float row_left = yes_cursor.position.x;
  const float row_right = no_label.position.x + no_w;
  const float panel_center = panel.position.x + (panel.size.x * 0.5f);

  CHECK((row_left + row_right) * 0.5f == panel_center);
}

TEST_CASE("prompt_box_render: selection swaps the cursor without moving the label columns") {
  RecordingRenderer r_yes;
  RecordingRenderer r_no;
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  corundum::ui::prompt_box_render(r_yes, style, border, "Leave?", true, viewport);
  corundum::ui::prompt_box_render(r_no, style, border, "Leave?", false, viewport);

  // Cursors follow the highlighted option...
  CHECK(std::get<DrawText>(r_yes.log[10]).text == "> ");
  CHECK(std::get<DrawText>(r_yes.log[12]).text == "  ");
  CHECK(std::get<DrawText>(r_no.log[10]).text == "  ");
  CHECK(std::get<DrawText>(r_no.log[12]).text == "> ");

  // ...but the label columns stay put, since draw_option always advances by the cursor width.
  CHECK(std::get<DrawText>(r_yes.log[11]).position.x == std::get<DrawText>(r_no.log[11]).position.x);
  CHECK(std::get<DrawText>(r_yes.log[13]).position.x == std::get<DrawText>(r_no.log[13]).position.x);

  // Colours also follow selection.
  CHECK(std::get<DrawText>(r_yes.log[11]).colour.r == style.selected.r);
  CHECK(std::get<DrawText>(r_no.log[11]).colour.r == style.choice.r);
}

TEST_CASE("prompt_box_render: panel respects the minimum width and grows for a long question") {
  const corundum::ui::NinePatchBorder border = make_border();
  const corundum::ui::PanelStyle style{};
  const corundum::core::math::Vec2 viewport{.x = 1280.f, .y = 720.f};

  {
    RecordingRenderer r;
    corundum::ui::prompt_box_render(r, style, border, "Enter?", true, viewport);
    CHECK(std::get<DrawRect>(r.log[0]).size.x == 200.f);
  }
  {
    RecordingRenderer r;
    const std::string_view long_question = "A considerably longer question?";
    corundum::ui::prompt_box_render(r, style, border, long_question, true, viewport);
    const float q_w = r.measure_text(style.font_id, long_question, style.font_size_body);
    CHECK(std::get<DrawRect>(r.log[0]).size.x == q_w + 64.f); // content + 2 × k_pad_x
  }
}

TEST_CASE("hovered_row: uniform list hits inside rows and misses outside them") {
  const corundum::ui::ListHit list{
      .row_pos = {.x = 100.f, .y = 50.f},
      .row_width = 200.f,
      .row_height = 20.f,
      .first_row = 0,
      .visible_rows = 3,
  };

  CHECK(corundum::ui::hovered_row({.x = 150.f, .y = 55.f}, list) == 0);
  CHECK(corundum::ui::hovered_row({.x = 150.f, .y = 75.f}, list) == 1);
  CHECK(corundum::ui::hovered_row({.x = 150.f, .y = 95.f}, list) == 2);
  // Between the last drawn row and the list bottom.
  CHECK(corundum::ui::hovered_row({.x = 150.f, .y = 115.f}, list) == -1);
  // Left and right of the row band.
  CHECK(corundum::ui::hovered_row({.x = 99.f, .y = 55.f}, list) == -1);
  CHECK(corundum::ui::hovered_row({.x = 301.f, .y = 55.f}, list) == -1);
  // Above the first row.
  CHECK(corundum::ui::hovered_row({.x = 150.f, .y = 40.f}, list) == -1);
}

TEST_CASE("hovered_row: a scroll offset returns absolute row indices") {
  const corundum::ui::ListHit list{
      .row_pos = {.x = 0.f, .y = 200.f},
      .row_width = 100.f,
      .row_height = 10.f,
      .first_row = 5,
      .visible_rows = 2,
  };

  CHECK(corundum::ui::hovered_row({.x = 10.f, .y = 205.f}, list) == 5);
  CHECK(corundum::ui::hovered_row({.x = 10.f, .y = 215.f}, list) == 6);
  CHECK(corundum::ui::hovered_row({.x = 10.f, .y = 225.f}, list) == -1);
}

TEST_CASE("hovered_row: an empty or degenerate list hits nothing") {
  CHECK(corundum::ui::hovered_row({.x = 0.f, .y = 0.f}, corundum::ui::ListHit{}) == -1);
  CHECK(corundum::ui::hovered_row({.x = 0.f, .y = 0.f},
                                  corundum::ui::ListHit{.row_width = 10.f, .row_height = 0.f, .visible_rows = 3}) ==
        -1);
}

TEST_CASE("hovered_row: non-uniform RowRects map to their index") {
  const std::array<corundum::ui::RowRect, 2> rows = {
      corundum::ui::RowRect{.pos = {.x = 0.f, .y = 10.f}, .width = 50.f, .height = 20.f},
      corundum::ui::RowRect{.pos = {.x = 0.f, .y = 50.f}, .width = 50.f, .height = 20.f},
  };
  CHECK(corundum::ui::hovered_row({.x = 5.f, .y = 15.f}, rows) == 0);
  CHECK(corundum::ui::hovered_row({.x = 5.f, .y = 55.f}, rows) == 1);
  // The gap between the two rows.
  CHECK(corundum::ui::hovered_row({.x = 5.f, .y = 40.f}, rows) == -1);
}

TEST_CASE("scroll_row_delta: wheel up moves up one row per notch and truncates fractions") {
  CHECK(corundum::ui::scroll_row_delta(0.f) == 0);
  CHECK(corundum::ui::scroll_row_delta(1.f) == -1);
  CHECK(corundum::ui::scroll_row_delta(-1.f) == 1);
  CHECK(corundum::ui::scroll_row_delta(1.9f) == -1);
  CHECK(corundum::ui::scroll_row_delta(-0.4f) == 0);
}
