#include <doctest/doctest.h>

#include "font_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <print>

namespace fs = std::filesystem;

namespace {

  // Bundled with the test target via CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR.
  // Keeps the test deterministic without reaching outside the test sandbox.
  fs::path fixture_font_path() {
    return fs::path(CORUNDUM_LIFECYCLE_TEST_FIXTURES_DIR) / "fonts" / "NotoSans.ttf";
  }

} // namespace

TEST_CASE("FontAtlas::bake: rasterises a basic ASCII set within atlas width and produces non-empty metrics") {
  FT_Library lib{nullptr};
  REQUIRE(FT_Init_FreeType(&lib) == 0);

  corundum::platform::glfw::FontAtlas atlas;
  REQUIRE(atlas.load(lib, fixture_font_path().string()));

  const corundum::platform::glfw::BakedSize baked = atlas.bake(16);

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

  // Every glyph rect must fit inside the atlas bounds.
  for (unsigned char c = 32; c < 128; ++c) {
    const auto &g = baked.glyphs[c];
    if (g.width == 0 && g.height == 0)
      continue;
    CHECK(g.atlas_x >= 0);
    CHECK(g.atlas_y >= 0);
    CHECK(g.atlas_x + g.width <= baked.atlas_w);
    CHECK(g.atlas_y + g.height <= baked.atlas_h);
  }

  FT_Done_FreeType(lib);
}

TEST_CASE("FontAtlas::bake: face destruction order is correct when the library outlives the atlas") {
  // A regression for the old per-atlas FT_Library ownership: faces reference
  // their owning library, so destroying the library first crashes. The shared
  // library refactor must keep that contract intact.
  FT_Library lib{nullptr};
  REQUIRE(FT_Init_FreeType(&lib) == 0);

  {
    corundum::platform::glfw::FontAtlas atlas;
    REQUIRE(atlas.load(lib, fixture_font_path().string()));
    const auto baked = atlas.bake(16);
    CHECK(baked.atlas_w > 0);
  }

  // Library is still valid; must not have been touched by atlas destruction.
  FT_Face probe{nullptr};
  CHECK(FT_New_Face(lib, fixture_font_path().string().c_str(), 0, &probe) == 0);
  if (probe)
    FT_Done_Face(probe);

  FT_Done_FreeType(lib);
}
