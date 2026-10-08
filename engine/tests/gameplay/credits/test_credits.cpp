// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/gameplay/credits/loader.hpp>
#include <corundum/gameplay/screens/credits.hpp>
#include <corundum/ui/panel_style.hpp>

#include "temp_dir.hpp"
#include "ui/recording_renderer.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <variant>

namespace fs = std::filesystem;
namespace credits = corundum::gameplay::credits;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  corundum::test::TempDir temp_dir(std::string_view tag) {
    return corundum::test::TempDir{"crpg_test_credits_", tag};
  }

  using corundum::platform::DrawSprite;
  using corundum::platform::DrawText;
  using corundum::test::RecordingRenderer;

  std::size_t sprite_count(const RecordingRenderer &r) {
    std::size_t count = 0;
    for (const RecordingRenderer::DrawCall &call : r.log)
      if (std::get_if<DrawSprite>(&call) != nullptr)
        ++count;
    return count;
  }

  std::optional<corundum::core::math::Colour> text_colour(const RecordingRenderer &r, std::string_view text) {
    for (const RecordingRenderer::DrawCall &call : r.log) {
      if (const auto *draw = std::get_if<DrawText>(&call); draw != nullptr && draw->text == text)
        return draw->colour;
    }
    return std::nullopt;
  }

} // namespace

TEST_CASE("credits loader: parses sections with headings and lines") {
  const auto dir = temp_dir("load");
  const auto path = dir / "credits.json";
  write_file(path, R"({
    "sections": [
      { "heading": "Design", "lines": ["Ada", "Grace"] },
      { "heading": "Engine", "lines": ["Corundum"] }
    ]
  })");

  const auto result = credits::load_credits_file(path);
  REQUIRE(result.has_value());
  REQUIRE(result->sections.size() == 2);
  CHECK(result->sections[0].heading == "Design");
  REQUIRE(result->sections[0].lines.size() == 2);
  CHECK(result->sections[0].lines[0] == "Ada");
  CHECK(result->sections[0].lines[1] == "Grace");
  CHECK(result->sections[1].heading == "Engine");
  CHECK(result->sections[1].lines[0] == "Corundum");
}

TEST_CASE("credits loader: an empty sections array is valid") {
  const auto dir = temp_dir("empty");
  const auto path = dir / "credits.json";
  write_file(path, R"({"sections": []})");

  const auto result = credits::load_credits_file(path);
  REQUIRE(result.has_value());
  CHECK(result->sections.empty());
}

TEST_CASE("credits loader: title is optional and parses when present") {
  const auto dir = temp_dir("title");

  const auto with_title = dir / "with_title.json";
  write_file(with_title, R"({"title": "Project X", "sections": []})");
  const auto r1 = credits::load_credits_file(with_title);
  REQUIRE(r1.has_value());
  CHECK(r1->title == "Project X");

  const auto without_title = dir / "without_title.json";
  write_file(without_title, R"({"sections": []})");
  const auto r2 = credits::load_credits_file(without_title);
  REQUIRE(r2.has_value());
  CHECK(r2->title.empty());
}

TEST_CASE("credits loader: background is optional and parses when present") {
  const auto dir = temp_dir("background");

  const auto with_background = dir / "with_background.json";
  write_file(with_background, R"({"background": "x.png", "sections": []})");
  const auto r1 = credits::load_credits_file(with_background);
  REQUIRE(r1.has_value());
  CHECK(r1->background == "x.png");

  const auto without_background = dir / "without_background.json";
  write_file(without_background, R"({"sections": []})");
  const auto r2 = credits::load_credits_file(without_background);
  REQUIRE(r2.has_value());
  CHECK(r2->background.empty());
}

TEST_CASE("credits loader: a missing or malformed file is reported, not fatal") {
  const auto dir = temp_dir("errors");
  const auto missing = dir / "missing.json";
  CHECK_FALSE(credits::load_credits_file(missing).has_value());

  const auto malformed = dir / "malformed.json";
  write_file(malformed, "not json");
  CHECK_FALSE(credits::load_credits_file(malformed).has_value());

  const auto no_sections = dir / "no_sections.json";
  write_file(no_sections, R"({"other": []})");
  CHECK_FALSE(credits::load_credits_file(no_sections).has_value());

  const auto bad_line = dir / "bad_line.json";
  write_file(bad_line, R"({"sections": [{"heading": "x", "lines": [1]}]})");
  CHECK_FALSE(credits::load_credits_file(bad_line).has_value());

  const auto bad_title = dir / "bad_title.json";
  write_file(bad_title, R"({"title": 1, "sections": []})");
  CHECK_FALSE(credits::load_credits_file(bad_title).has_value());

  const auto bad_background = dir / "bad_background.json";
  write_file(bad_background, R"({"background": 1, "sections": []})");
  CHECK_FALSE(credits::load_credits_file(bad_background).has_value());
}

TEST_CASE("credits render: a background image is drawn over the backdrop and switches text to ink") {
  corundum::ui::PanelStyle style{};
  corundum::gameplay::screens::CreditsState state;
  state.title = "Title";
  state.background_texture = 1;
  state.background_size = {.x = 2560.f, .y = 1440.f};

  RecordingRenderer r;
  corundum::gameplay::screens::credits_panel_render(r, style, {.x = 1280.f, .y = 720.f}, state);

  CHECK(sprite_count(r) == 1);
  const std::optional<corundum::core::math::Colour> colour = text_colour(r, "Title");
  REQUIRE(colour.has_value());
  CHECK(colour->r == 90);
  CHECK(colour->g == 30);
  CHECK(colour->b == 25);
}

TEST_CASE("credits render: without a background the flat backdrop and light text are kept") {
  corundum::ui::PanelStyle style{};
  corundum::gameplay::screens::CreditsState state;
  state.title = "Title";

  RecordingRenderer r;
  corundum::gameplay::screens::credits_panel_render(r, style, {.x = 1280.f, .y = 720.f}, state);

  CHECK(sprite_count(r) == 0);
  const std::optional<corundum::core::math::Colour> colour = text_colour(r, "Title");
  REQUIRE(colour.has_value());
  CHECK(colour->r == style.speaker.r);
  CHECK(colour->g == style.speaker.g);
  CHECK(colour->b == style.speaker.b);
}
