#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Forward-declare FreeType handle so callers don't need <ft2build.h>.
using FT_Face = struct FT_FaceRec_ *;
using FT_Library = struct FT_LibraryRec_ *;

namespace corundum::platform::glfw {

  /// Metrics for a single glyph within the atlas texture.
  struct GlyphInfo {
    int atlas_x{};    ///< Top-left X within the atlas image.
    int atlas_y{};    ///< Top-left Y within the atlas image.
    int width{};      ///< Glyph bitmap width in pixels.
    int height{};     ///< Glyph bitmap height in pixels.
    int bearing_x{};  ///< Horizontal bearing from the glyph origin.
    int bearing_y{};  ///< Vertical bearing from the glyph origin (FreeType Y-up).
    float advance_x{}; ///< Horizontal advance to next glyph origin.
  };

  /// Pixel data and per-glyph layout for one (font, char_size) combination.
  /// The pixel data is RGBA: R=G=B=255, A=coverage. Upload to the GPU as RGBA8.
  struct BakedSize {
    int atlas_w{};
    int atlas_h{};
    std::vector<uint8_t> pixels; ///< RGBA8, row-major, atlas_w * atlas_h * 4 bytes.
    std::array<GlyphInfo, 128> glyphs{};
  };

  /// FreeType face wrapper. Holds the FT_Face so multiple sizes can be baked from one load.
  struct FontAtlas {
    FT_Library library{nullptr};
    FT_Face face{nullptr};
    std::string path;

    FontAtlas() = default;
    ~FontAtlas();

    FontAtlas(const FontAtlas &) = delete;
    FontAtlas &operator=(const FontAtlas &) = delete;
    FontAtlas(FontAtlas &&) noexcept;
    FontAtlas &operator=(FontAtlas &&) noexcept;

    /// Load the FreeType face from @p font_path.
    /// @return false if FreeType fails to open the file.
    [[nodiscard]] bool load(std::string_view font_path);

    /// Rasterise ASCII glyphs at @p char_size pixels and return the atlas pixel data.
    /// @pre load() has been called and returned true.
    [[nodiscard]] BakedSize bake(uint32_t char_size) const;
  };

} // namespace corundum::platform::glfw
