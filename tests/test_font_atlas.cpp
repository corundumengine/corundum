// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "font_atlas.hpp"

#include <cstddef>
#include <cstdint>
#include <ft2build.h>  // NOLINT(misc-include-cleaner): shim that defines FT_FREETYPE_H
#include FT_FREETYPE_H // NOLINT(misc-include-cleaner): macro include resolved through ft2build.h

#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace fs = std::filesystem;
namespace pg = corundum::platform::glfw;

namespace {

  // Must match the packer's border; kept in sync by the layout test below.
  constexpr int k_padding = 1;

  // Bundled with the test target via CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR.
  // Keeps the test deterministic without reaching outside the test sandbox.
  fs::path fixture_font_path() {
    return fs::path(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR) / "fonts" / "NotoSans.ttf";
  }

  /// Owns the FreeType library for a test case so FT_Done_FreeType still runs if a
  /// REQUIRE aborts the case before the end.
  struct FreeTypeLibrary {
    FT_Library lib{nullptr};

    FreeTypeLibrary() = default;

    ~FreeTypeLibrary() {
      if (lib != nullptr)
        FT_Done_FreeType(lib);
    }

    FreeTypeLibrary(const FreeTypeLibrary &) = delete;
    FreeTypeLibrary &operator=(const FreeTypeLibrary &) = delete;
    FreeTypeLibrary(FreeTypeLibrary &&other) = delete;
    FreeTypeLibrary &operator=(FreeTypeLibrary &&other) = delete;
  };

  /// Bake @p char_size, aborting the case if FreeType rejects the request.
  pg::BakedSize require_bake(const pg::FontAtlas &atlas, uint32_t char_size) {
    std::expected<pg::BakedSize, std::string> baked = atlas.bake(char_size);
    REQUIRE(baked.has_value());
    return std::move(*baked);
  }

  /// Assert every packed glyph rectangle sits inside the atlas, leaving a uniform
  /// one-pixel border on all four sides.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
  void check_glyph_bounds(const pg::BakedSize &baked) {
    for (std::size_t c = pg::k_first_baked_ascii; c < pg::k_ascii_count; ++c) {
      const auto &g = baked.glyphs[c];
      if (g.width == 0 || g.height == 0)
        continue;
      CHECK(g.atlas_x >= k_padding);
      CHECK(g.atlas_y >= k_padding);
      CHECK(g.atlas_x + g.width + k_padding <= baked.atlas_w);
      CHECK(g.atlas_y + g.height + k_padding <= baked.atlas_h);
    }
  }

  /// Assert @p glyph's rectangle is opaque white in RGB (alpha carries coverage)
  /// and return how many of its cells actually contain ink.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
  int count_inked_pixels(const pg::BakedSize &baked, const pg::GlyphInfo &glyph) {
    int inked = 0;
    for (int row = 0; row < glyph.height; ++row)
      for (int col = 0; col < glyph.width; ++col) {
        const std::size_t row_offset =
            static_cast<std::size_t>(glyph.atlas_y + row) * static_cast<std::size_t>(baked.atlas_w);
        const std::size_t offset = (row_offset + static_cast<std::size_t>(glyph.atlas_x + col)) * 4;
        CHECK(baked.pixels[offset + 0] == 255);
        CHECK(baked.pixels[offset + 1] == 255);
        CHECK(baked.pixels[offset + 2] == 255);
        if (baked.pixels[offset + 3] > 0)
          ++inked;
      }
    return inked;
  }

} // namespace

TEST_CASE("FontAtlas::bake: rasterises a basic ASCII set within atlas width and produces non-empty metrics") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);

  // Scope the atlas so it is destroyed before FT_Done_FreeType: a face references
  // its owning library, so the library must outlive every face loaded from it.
  {
    pg::FontAtlas atlas;
    REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));

    const pg::BakedSize baked = require_bake(atlas, 16);

    CHECK(baked.atlas_w > 0);
    CHECK(baked.atlas_h > 0);
    // The shelf packer clamps width at 512; a 16 px Latin alphabet must fit comfortably.
    CHECK(baked.atlas_w <= 512);

    // Pixel buffer is RGBA8, fully populated.
    CHECK(baked.pixels.size() == static_cast<std::size_t>(baked.atlas_w) * baked.atlas_h * 4);

    // Glyph metrics for 'M' must be present and positive.
    const auto &glyph_m = baked.glyphs[static_cast<unsigned char>('M')];
    CHECK(glyph_m.width > 0);
    CHECK(glyph_m.height > 0);
    CHECK(glyph_m.advance_x > 0.f);

    // Regression: space has a 0×0 bitmap (so the packer drops it), but its
    // advance_x must still be populated — draw(DrawText) uses it to advance the
    // pen between words and measure_text() sums it for width reporting. An
    // empty advance rendered "Hello World" as "HelloWorld" in the dialog box.
    const auto &glyph_space = baked.glyphs[static_cast<unsigned char>(' ')];
    CHECK(glyph_space.width == 0);
    CHECK(glyph_space.height == 0);
    CHECK(glyph_space.advance_x > 0.f);

    check_glyph_bounds(baked);
  } // atlas destroyed while lib is still alive
}

TEST_CASE("FontAtlas::bake: atlas layout, padding, and RGBA encoding are consistent") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);
  pg::FontAtlas atlas;
  REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));
  const pg::BakedSize baked = require_bake(atlas, 20);

  CHECK(baked.pixels.size() == static_cast<std::size_t>(baked.atlas_w) * baked.atlas_h * 4);
  // The top-left corner is part of the transparent border, never a glyph cell.
  CHECK(baked.pixels[0] == 0);
  CHECK(baked.pixels[1] == 0);
  CHECK(baked.pixels[2] == 0);
  CHECK(baked.pixels[3] == 0);

  check_glyph_bounds(baked);

  const auto &glyph_m = baked.glyphs[static_cast<unsigned char>('M')];
  CHECK(count_inked_pixels(baked, glyph_m) > 0);
}

TEST_CASE("FontAtlas::bake: a larger char_size scales glyph metrics and the atlas") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);
  pg::FontAtlas atlas;
  REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));

  const pg::BakedSize small = require_bake(atlas, 12);
  const pg::BakedSize large = require_bake(atlas, 32);

  const auto &small_m = small.glyphs[static_cast<unsigned char>('M')];
  const auto &large_m = large.glyphs[static_cast<unsigned char>('M')];
  CHECK(large_m.width > small_m.width);
  CHECK(large_m.height > small_m.height);
  CHECK(large_m.advance_x > small_m.advance_x);
  CHECK(large.atlas_h > small.atlas_h);
  CHECK(large.atlas_w <= 512);
}

TEST_CASE("FontAtlas::load: a missing file returns false and leaves the atlas unloaded") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);

  pg::FontAtlas atlas;
  CHECK_FALSE(atlas.load(ft.lib, "/nonexistent/corundum/NoSuchFont.ttf"));
  CHECK(atlas.face == nullptr);
}

TEST_CASE("FontAtlas::load: a repeated load replaces the face and stays bakeable") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);
  pg::FontAtlas atlas;

  REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));
  REQUIRE(atlas.bake(16).has_value());

  // Reloading must free the previous face rather than overwrite (and leak) it.
  REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));
  CHECK(atlas.path == fixture_font_path().string());
  CHECK(require_bake(atlas, 16).atlas_w > 0);
}

TEST_CASE("FontAtlas: move construction and assignment transfer face ownership") {
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);
  pg::FontAtlas source;
  REQUIRE(source.load(ft.lib, fixture_font_path().string()));
  CHECK(source.face != nullptr);

  pg::FontAtlas moved{std::move(source)};
  // NOLINTNEXTLINE(bugprone-use-after-move): reading the moved-from state is the point.
  CHECK(source.face == nullptr);
  CHECK(moved.face != nullptr);
  CHECK(require_bake(moved, 16).atlas_w > 0);

  pg::FontAtlas assigned;
  assigned = std::move(moved);
  // NOLINTNEXTLINE(bugprone-use-after-move): reading the moved-from state is the point.
  CHECK(moved.face == nullptr);
  CHECK(assigned.face != nullptr);
  CHECK(require_bake(assigned, 16).atlas_w > 0);
}

TEST_CASE("FontAtlas::bake: face destruction order is correct when the library outlives the atlas") {
  // A regression for the old per-atlas FT_Library ownership: faces reference
  // their owning library, so destroying the library first crashes. The shared
  // library refactor must keep that contract intact.
  FreeTypeLibrary ft;
  REQUIRE(FT_Init_FreeType(&ft.lib) == 0);

  {
    pg::FontAtlas atlas;
    REQUIRE(atlas.load(ft.lib, fixture_font_path().string()));
    const auto baked = require_bake(atlas, 16);
    CHECK(baked.atlas_w > 0);
  }

  // Library is still valid; must not have been touched by atlas destruction.
  FT_Face probe{nullptr};
  CHECK(FT_New_Face(ft.lib, fixture_font_path().string().c_str(), 0, &probe) == 0);
  if (probe != nullptr)
    FT_Done_Face(probe);
}
