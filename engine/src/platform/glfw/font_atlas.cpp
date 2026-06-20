#include "font_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstring>
#include <print>

namespace corundum::platform::glfw {

  FontAtlas::~FontAtlas() {
    if (face)
      FT_Done_Face(face);
    if (library)
      FT_Done_FreeType(library);
  }

  FontAtlas::FontAtlas(FontAtlas &&o) noexcept : library{o.library}, face{o.face}, path{std::move(o.path)} {
    o.library = nullptr;
    o.face = nullptr;
  }

  FontAtlas &FontAtlas::operator=(FontAtlas &&o) noexcept {
    if (this != &o) {
      if (face)
        FT_Done_Face(face);
      if (library)
        FT_Done_FreeType(library);
      library = o.library;
      face = o.face;
      path = std::move(o.path);
      o.library = nullptr;
      o.face = nullptr;
    }
    return *this;
  }

  bool FontAtlas::load(std::string_view font_path) {
    path = std::string{font_path};
    if (FT_Init_FreeType(&library) != 0) {
      std::println(stderr, "[font_atlas] FT_Init_FreeType failed");
      return false;
    }
    if (FT_New_Face(library, path.c_str(), 0, &face) != 0) {
      std::println(stderr, "[font_atlas] FT_New_Face failed for '{}'", path);
      FT_Done_FreeType(library);
      library = nullptr;
      return false;
    }
    return true;
  }

  BakedSize FontAtlas::bake(uint32_t char_size) const {
    FT_Set_Pixel_Sizes(face, 0, char_size);

    // First pass: measure total atlas dimensions (single row, baseline at y=char_size).
    int total_w = 0;
    int max_extent = 0; // max (dst_y + rows) = max pixels needed below the atlas top
    for (unsigned char c = 32; c < 128; ++c) {
      if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
        continue;
      total_w += static_cast<int>(face->glyph->bitmap.width) + 1; // +1 pixel gap
      const int dst_y = static_cast<int>(char_size) - face->glyph->bitmap_top;
      const int extent = dst_y + static_cast<int>(face->glyph->bitmap.rows);
      max_extent = std::max(max_extent, extent);
    }

    const int atlas_w = total_w;
    const int atlas_h = max_extent + 2; // 2 px bottom padding for descenders

    BakedSize result;
    result.atlas_w = atlas_w;
    result.atlas_h = atlas_h;
    // RGBA8: R=G=B=255, A=coverage. Initialise to fully transparent.
    result.pixels.assign(static_cast<std::size_t>(atlas_w * atlas_h) * 4, 0);

    // Second pass: rasterise glyphs into the atlas.
    int pen_x = 0;
    for (unsigned char c = 32; c < 128; ++c) {
      if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
        continue;

      const FT_GlyphSlot g = face->glyph;
      const int gw = static_cast<int>(g->bitmap.width);
      const int gh = static_cast<int>(g->bitmap.rows);

      // Baseline is char_size pixels from the top.
      const int baseline = static_cast<int>(char_size);
      const int dst_y = baseline - g->bitmap_top;

      for (int row = 0; row < gh; ++row) {
        const int y = dst_y + row;
        if (y < 0 || y >= atlas_h)
          continue;
        for (int col = 0; col < gw; ++col) {
          const int x = pen_x + col;
          if (x < 0 || x >= atlas_w)
            continue;
          const uint8_t coverage = g->bitmap.buffer[static_cast<std::size_t>(row * gw + col)];
          const auto idx = static_cast<std::size_t>((y * atlas_w + x) * 4);
          result.pixels[idx + 0] = 255;
          result.pixels[idx + 1] = 255;
          result.pixels[idx + 2] = 255;
          result.pixels[idx + 3] = coverage;
        }
      }

      GlyphInfo &info = result.glyphs[c];
      info.atlas_x = pen_x;
      info.atlas_y = dst_y; // actual row in atlas where this glyph's bitmap starts
      info.width = gw;
      info.height = gh;
      info.bearing_x = g->bitmap_left;
      info.bearing_y = g->bitmap_top;
      info.advance_x = static_cast<float>(g->advance.x >> 6);

      pen_x += gw + 1;
    }

    return result;
  }

} // namespace corundum::platform::glfw
