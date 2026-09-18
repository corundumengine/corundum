// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "font_atlas.hpp"

#include <cstdint>
#include <cstdio>
#include <ft2build.h> // NOLINT(misc-include-cleaner): shim that defines FT_FREETYPE_H
#include <string_view>
#include <utility>
#include <vector>
#include FT_FREETYPE_H // NOLINT(misc-include-cleaner): macro include resolved through ft2build.h

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <format>
#include <print>

namespace corundum::platform::glfw {
  namespace {

    constexpr int k_max_atlas_width = 512;
    constexpr int k_padding = 1;

    /// Rasterised coverage per codepoint. GlyphInfo carries the matching metrics,
    /// so the packer and blit read both arrays by the same index.
    using CoverageSet = std::array<std::vector<uint8_t>, k_ascii_count>;

    /// Rasterises printable ASCII into @p coverage, recording each glyph's metrics
    /// in @p glyphs.
    void rasterise_glyphs(FT_Face face, CoverageSet &coverage, std::array<GlyphInfo, k_ascii_count> &glyphs) {
      for (std::size_t codepoint = k_first_baked_ascii; codepoint < k_ascii_count; ++codepoint) {
        // NOLINTNEXTLINE(bugprone-signed-bitwise): FT_LOAD_RENDER is defined as (1L << 2).
        if (FT_Load_Char(face, static_cast<unsigned long>(codepoint), FT_LOAD_RENDER) != 0)
          continue;

        const FT_GlyphSlotRec_ *slot = face->glyph;
        // Metrics are written for every loaded glyph, including zero-area ones
        // (notably space) that the packer drops — their advance_x is still needed
        // by draw(DrawText) to position the pen and by measure_text() to report width.
        GlyphInfo &info = glyphs[codepoint];
        info.width = static_cast<int>(slot->bitmap.width);
        info.height = static_cast<int>(slot->bitmap.rows);
        info.bearing_x = slot->bitmap_left;
        info.bearing_y = slot->bitmap_top;
        info.advance_x = static_cast<float>(slot->advance.x) / 64.0f;

        std::vector<uint8_t> &pixels = coverage[codepoint];
        const auto row_stride = static_cast<std::size_t>(info.width);
        pixels.resize(row_stride * static_cast<std::size_t>(info.height));
        for (int row = 0; row < info.height; ++row) {
          const std::size_t row_offset = static_cast<std::size_t>(row) * row_stride;
          const std::size_t source_offset =
              static_cast<std::size_t>(row) * static_cast<std::size_t>(slot->bitmap.pitch);
          std::memcpy(pixels.data() + row_offset, slot->bitmap.buffer + source_offset, row_stride);
        }
      }
    }

    /// Extent of the shelf-packed atlas, including its one-pixel border.
    struct AtlasExtent {
      int width{};
      int height{};
    };

    /// Shelf-packs the rasterised glyphs into rows bounded by @p k_max_atlas_width,
    /// writing each glyph's atlas rectangle into @p glyphs.
    AtlasExtent pack_glyphs(std::array<GlyphInfo, k_ascii_count> &glyphs) {
      std::array<std::size_t, k_ascii_count> order{};
      std::size_t count = 0;
      for (std::size_t codepoint = k_first_baked_ascii; codepoint < k_ascii_count; ++codepoint)
        if (glyphs[codepoint].width > 0 || glyphs[codepoint].height > 0)
          order[count++] = codepoint;
      std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count),
                [&](std::size_t a, std::size_t b) { return glyphs[a].height > glyphs[b].height; });

      int pen_x = k_padding;
      int pen_y = k_padding;
      int shelf_height = 0;
      int used_width = 0;
      for (std::size_t i = 0; i < count; ++i) {
        GlyphInfo &info = glyphs[order[i]];
        if (pen_x + info.width + k_padding > k_max_atlas_width) {
          pen_y += shelf_height + k_padding;
          pen_x = k_padding;
          shelf_height = 0;
        }
        info.atlas_x = pen_x;
        info.atlas_y = pen_y;
        pen_x += info.width + k_padding;
        shelf_height = std::max(shelf_height, info.height);
        used_width = std::max(used_width, pen_x);
      }

      // used_width already includes the inter-glyph padding that follows the last
      // glyph, so it is the right edge (one-pixel margin, matching the bottom).
      return {.width = std::max(1, used_width), .height = std::max(1, pen_y + shelf_height + k_padding)};
    }

    /// Copies each packed glyph's coverage into @p result as white RGB + coverage alpha.
    void blit_glyphs(const CoverageSet &coverage, const std::array<GlyphInfo, k_ascii_count> &glyphs,
                     BakedSize &result) {
      result.pixels.assign(static_cast<std::size_t>(result.atlas_w) * result.atlas_h * 4, 0);
      for (std::size_t codepoint = k_first_baked_ascii; codepoint < k_ascii_count; ++codepoint) {
        const GlyphInfo &info = glyphs[codepoint];
        if (info.width == 0 || info.height == 0)
          continue;

        const std::vector<uint8_t> &pixels = coverage[codepoint];
        const auto row_stride = static_cast<std::size_t>(info.width);
        for (int row = 0; row < info.height; ++row)
          for (int col = 0; col < info.width; ++col) {
            const std::size_t row_offset =
                static_cast<std::size_t>(info.atlas_y + row) * static_cast<std::size_t>(result.atlas_w);
            const std::size_t offset = (row_offset + static_cast<std::size_t>(info.atlas_x + col)) * 4;
            result.pixels[offset + 0] = 255;
            result.pixels[offset + 1] = 255;
            result.pixels[offset + 2] = 255;
            result.pixels[offset + 3] = pixels[(static_cast<std::size_t>(row) * row_stride) + col];
          }
      }
    }

  } // namespace

  FontAtlas::~FontAtlas() {
    if (face != nullptr)
      FT_Done_Face(face);
  }

  FontAtlas::FontAtlas(FontAtlas &&other) noexcept : face{other.face}, path{std::move(other.path)} {
    other.face = nullptr;
  }

  FontAtlas &FontAtlas::operator=(FontAtlas &&other) noexcept {
    if (this != &other) {
      if (face != nullptr)
        FT_Done_Face(face);
      face = other.face;
      path = std::move(other.path);
      other.face = nullptr;
    }
    return *this;
  }

  bool FontAtlas::load(FT_Library lib, std::string_view font_path) {
    // A second load would otherwise overwrite (and leak) the existing face.
    if (face != nullptr) {
      FT_Done_Face(face);
      face = nullptr;
    }

    path = std::string{font_path};
    if (FT_New_Face(lib, path.c_str(), 0, &face) != 0) {
      std::println(stderr, "[font_atlas] FT_New_Face failed for '{}'", path);
      return false;
    }
    return true;
  }

  std::expected<BakedSize, std::string> FontAtlas::bake(uint32_t char_size) const {
    if (FT_Set_Pixel_Sizes(face, 0, char_size) != 0)
      return std::unexpected(std::format("[font_atlas] FT_Set_Pixel_Sizes failed for size {}", char_size));

    CoverageSet coverage{};
    BakedSize result;
    rasterise_glyphs(face, coverage, result.glyphs);

    const AtlasExtent extent = pack_glyphs(result.glyphs);
    result.atlas_w = extent.width;
    result.atlas_h = extent.height;
    blit_glyphs(coverage, result.glyphs, result);
    return result;
  }

} // namespace corundum::platform::glfw
