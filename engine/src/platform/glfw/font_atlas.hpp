// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

// Forward-declare FreeType handle so callers don't need <ft2build.h>.
// These aliases spell FreeType's own type names, so the project's CamelCase rule can't apply.
// NOLINTBEGIN(readability-identifier-naming)
using FT_Face = struct FT_FaceRec_ *;
using FT_Library = struct FT_LibraryRec_ *;
// NOLINTEND(readability-identifier-naming)

namespace corundum::platform::glfw {

  /// Number of codepoints (0–255: ASCII + Latin-1 Supplement) an atlas reserves metrics for.
  inline constexpr std::size_t k_latin1_count = 256;
  /// First codepoint that is actually baked; control codes below it stay zeroed.
  inline constexpr std::size_t k_first_baked_ascii = 32;
  /// First baked codepoint of the Latin-1 Supplement; the C1 block (0x80–0x9F) before it is skipped.
  inline constexpr std::size_t k_first_baked_latin1 = 0xA0;

  /// Number of codepoints baked outside Latin-1.
  inline constexpr std::size_t k_extended_count = 9;

  /// Codepoints outside Latin-1 worth baking individually: hyphen, non-breaking
  /// hyphen, en/em dash, curly single/double quotes, and horizontal ellipsis —
  /// characters word processors substitute for their ASCII look-alikes via
  /// "smart punctuation".
  inline constexpr std::array<uint32_t, k_extended_count> k_extended_codepoints = {
      0x2010, 0x2011, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
  };

  /// Metrics for a single glyph within the atlas texture.
  struct GlyphInfo {
    int atlas_x{};     ///< Top-left X within the atlas image.
    int atlas_y{};     ///< Top-left Y within the atlas image.
    int width{};       ///< Glyph bitmap width in pixels.
    int height{};      ///< Glyph bitmap height in pixels.
    int bearing_x{};   ///< Horizontal bearing from the glyph origin.
    int bearing_y{};   ///< Vertical bearing from the glyph origin (FreeType Y-up).
    float advance_x{}; ///< Horizontal advance to next glyph origin.
  };

  /// A glyph from the extended (non-Latin-1) set. `codepoint` is always one of
  /// k_extended_codepoints; a codepoint FreeType could not render keeps zeroed
  /// metrics, which the renderer treats as a zero-width skip.
  struct ExtendedGlyph {
    uint32_t codepoint{};

    GlyphInfo info{};
  };

  /// Pixel data and per-glyph layout for one (font, char_size) combination.
  /// The pixel data is RGBA: R=G=B=255, A=coverage. Upload to the GPU as RGBA8.
  struct BakedSize {
    int atlas_w{};
    int atlas_h{};
    std::vector<uint8_t> pixels; ///< RGBA8, row-major, atlas_w * atlas_h * 4 bytes.
    std::array<GlyphInfo, k_latin1_count> glyphs{};
    std::array<ExtendedGlyph, k_extended_count> extended_glyphs{};
  };

  /// FreeType face wrapper. Holds the FT_Face so multiple sizes can be baked from one load.
  struct FontAtlas {
    FT_Face face{nullptr};
    std::string path;

    FontAtlas() = default;
    ~FontAtlas();

    FontAtlas(const FontAtlas &) = delete;
    FontAtlas &operator=(const FontAtlas &) = delete;
    FontAtlas(FontAtlas &&other) noexcept;
    FontAtlas &operator=(FontAtlas &&other) noexcept;

    /// Open the FreeType face from @p font_path using the shared library @p lib.
    /// @return false if FreeType fails to open the file.
    [[nodiscard]] bool load(FT_Library lib, std::string_view font_path);

    /// Rasterise Latin-1 Supplement plus the extended punctuation set at @p char_size
    /// pixels and return the atlas pixel data.
    /// @pre load() has been called and returned true.
    /// @return the baked atlas, or a message if FreeType rejected @p char_size.
    [[nodiscard]] std::expected<BakedSize, std::string> bake(uint32_t char_size) const;
  };

} // namespace corundum::platform::glfw
