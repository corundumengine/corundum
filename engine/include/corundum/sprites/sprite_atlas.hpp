// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::sprites {

  /// Sprite atlas JSON schema version this engine understands. Must match spritepacker's
  /// k_metadata_schema_version exactly — there is no legacy atlas format to stay compatible with,
  /// so a mismatch fails loudly instead of silently misreading fields.
  inline constexpr int k_sprite_atlas_schema_version = 2;

  /// How an atlas's AtlasSprite::pivot_x/pivot_y are measured. The enumerator names spell out both
  /// the reference box and the y-origin: the two bases are not comparable, and it is the flipped
  /// y-origin — not the box — that is easiest to get wrong. spritepacker writes TrimmedTopOrigin by
  /// default (fraction of the trimmed box, y from the top) and FullCanvasBottomOrigin when packed
  /// with `--pivot full-canvas` (fraction of the full untrimmed frame, y from the bottom). Resolve
  /// pivots through resolve_pivot() rather than reading pivot_x/pivot_y directly.
  enum class PivotBasis : std::uint8_t { TrimmedTopOrigin, FullCanvasBottomOrigin };

  /// A sprite pivot in the engine's canonical basis: a fraction of the full (untrimmed) frame with
  /// the origin at the bottom-left. This is the convention TilesetInfo/TilePivot (tilemap.hpp) uses,
  /// and the one the isometric ground-contact math expects.
  struct PivotPoint {
    float x = 0.f; ///< Fraction of the full frame width, from the left edge.
    float y = 0.f; ///< Fraction of the full frame height, from the bottom edge.
  };

  /// One packed sprite's placement and trim/pivot metadata, as written by spritepacker.
  /// `x/y/w/h` is the trimmed content's position in the atlas. `trim_x/trim_y` is the offset from
  /// the original untrimmed sprite's top-left corner to that trimmed region, and
  /// `source_width/source_height` are the original dimensions — together they let a consumer
  /// re-expand a sprite to its full (untrimmed) frame before applying its pivot.
  ///
  /// @note `pivot_x/pivot_y` are measured against the basis recorded in SpriteAtlas::pivot_basis,
  /// so they are not comparable across atlases of different bases; feed them to resolve_pivot()
  /// instead of consuming them directly.
  struct AtlasSprite {
    std::string name;

    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    int trim_x = 0;
    int trim_y = 0;

    /// Original dimensions. load_sprite_atlas fills these from w/h when absent, so 0 only appears
    /// on a hand-built sprite (where validation would reject it).
    int source_width = 0;
    int source_height = 0;

    float pivot_x = 0.f;
    float pivot_y = 0.f;
  };

  /// Top-level data parsed from a spritepacker atlas metadata JSON file (schema_version 2).
  struct SpriteAtlas {
    std::string path; ///< Image path verbatim from the JSON — NOT resolved against the atlas file's
                      ///< directory; callers resolve it relative to the atlas path they loaded.

    int width = 0;
    int height = 0;

    PivotBasis pivot_basis = PivotBasis::TrimmedTopOrigin;

    std::vector<AtlasSprite> sprites;
  };

  /// Load and validate a sprite atlas JSON file produced by the spritepacker tool.
  /// @param path Path to the atlas metadata JSON.
  /// @return SpriteAtlas on success, or an error string on any parse or validation failure.
  [[nodiscard]] std::expected<SpriteAtlas, std::string> load_sprite_atlas(const std::filesystem::path &path);

  /// Resolve @p sprite's pivot into the canonical full (untrimmed) frame, bottom-origin basis,
  /// collapsing @p basis so callers never re-derive the trimmed-box conversion themselves.
  /// @pre @p sprite.source_width and @p sprite.source_height are > 0 (guaranteed by load_sprite_atlas).
  /// @return Pivot as a fraction of the full frame, y measured from the bottom.
  [[nodiscard]] PivotPoint resolve_pivot(const AtlasSprite &sprite, PivotBasis basis) noexcept;

} // namespace corundum::sprites
