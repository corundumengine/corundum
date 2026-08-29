#include "font_atlas.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <array>
#include <cstring>
#include <print>

namespace corundum::platform::glfw {

  FontAtlas::~FontAtlas() {
    if (face)
      FT_Done_Face(face);
  }

  FontAtlas::FontAtlas(FontAtlas &&o) noexcept : face{o.face}, path{std::move(o.path)} {
    o.face = nullptr;
  }

  FontAtlas &FontAtlas::operator=(FontAtlas &&o) noexcept {
    if (this != &o) {
      if (face)
        FT_Done_Face(face);
      face = o.face;
      path = std::move(o.path);
      o.face = nullptr;
    }
    return *this;
  }

  bool FontAtlas::load(FT_Library lib, std::string_view font_path) {
    path = std::string{font_path};
    if (FT_New_Face(lib, path.c_str(), 0, &face) != 0) {
      std::println(stderr, "[font_atlas] FT_New_Face failed for '{}'", path);
      return false;
    }
    return true;
  }

  BakedSize FontAtlas::bake(uint32_t char_size) const {
    FT_Set_Pixel_Sizes(face, 0, char_size);

    BakedSize result;

    struct G {
      int w{};
      int h{};
      int bx{};
      int by{};
      float adv{};
      std::vector<uint8_t> cov;
    };

    std::array<G, 128> gs{};

    for (unsigned char c = 32; c < 128; ++c) {
      if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0)
        continue;
      const FT_GlyphSlot s = face->glyph;
      G &g = gs[c];
      g.w = int(s->bitmap.width);
      g.h = int(s->bitmap.rows);
      g.bx = s->bitmap_left;
      g.by = s->bitmap_top;
      g.adv = float(s->advance.x >> 6);
      // Size-independent metrics must be written for every loaded glyph,
      // including zero-area ones (notably space) that the packer drops — their
      // advance_x is still needed by draw(DrawText) to position the pen and by
      // measure_text() to report width.
      GlyphInfo &info = result.glyphs[c];
      info.bearing_x = g.bx;
      info.bearing_y = g.by;
      info.advance_x = g.adv;
      g.cov.resize(size_t(g.w) * g.h);
      for (int row = 0; row < g.h; ++row)
        std::memcpy(g.cov.data() + size_t(row) * g.w, s->bitmap.buffer + size_t(row) * s->bitmap.pitch, size_t(g.w));
    }

    constexpr int max_w = 512;
    constexpr int pad = 1;
    std::array<int, 128> order{};
    int n = 0;
    for (int c = 32; c < 128; ++c)
      if (gs[c].w > 0 || gs[c].h > 0)
        order[n++] = c;
    std::sort(order.begin(), order.begin() + n, [&](int a, int b) { return gs[a].h > gs[b].h; });

    int pen_x = pad;
    int pen_y = pad;
    int shelf_h = 0;
    int used_w = 0;
    for (int i = 0; i < n; ++i) {
      G &g = gs[order[i]];
      if (pen_x + g.w + pad > max_w) {
        pen_y += shelf_h + pad;
        pen_x = pad;
        shelf_h = 0;
      }
      GlyphInfo &info = result.glyphs[order[i]];
      info.atlas_x = pen_x;
      info.atlas_y = pen_y;
      info.width = g.w;
      info.height = g.h;
      pen_x += g.w + pad;
      shelf_h = std::max(shelf_h, g.h);
      used_w = std::max(used_w, pen_x);
    }
    result.atlas_w = std::max(1, used_w + pad);
    result.atlas_h = std::max(1, pen_y + shelf_h + pad);
    result.pixels.assign(size_t(result.atlas_w) * result.atlas_h * 4, 0);
    for (int i = 0; i < n; ++i) {
      const int c = order[i];
      const G &g = gs[c];
      const GlyphInfo &info = result.glyphs[c];
      for (int row = 0; row < g.h; ++row)
        for (int col = 0; col < g.w; ++col) {
          const size_t d = (size_t(info.atlas_y + row) * result.atlas_w + info.atlas_x + col) * 4;
          result.pixels[d + 0] = 255;
          result.pixels[d + 1] = 255;
          result.pixels[d + 2] = 255;
          result.pixels[d + 3] = g.cov[size_t(row) * g.w + col];
        }
    }
    return result;
  }

} // namespace corundum::platform::glfw
