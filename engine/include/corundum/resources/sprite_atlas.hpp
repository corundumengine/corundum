#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::resources {

  /// Sprite atlas JSON schema version this engine understands. Must match spritepacker's
  /// k_metadata_schema_version exactly — there is no legacy atlas format to stay compatible with,
  /// so a mismatch fails loudly instead of silently misreading fields.
  inline constexpr int k_sprite_atlas_schema_version = 2;

  /// One packed sprite's placement and trim/pivot metadata, as written by spritepacker.
  /// `x/y/w/h` is the trimmed content's position in the atlas. `trim_x/trim_y` is the offset from
  /// the original untrimmed sprite's top-left corner to that trimmed region, and
  /// `source_width/source_height` are the original dimensions — together they let a consumer
  /// re-expand a sprite to its authored canvas before applying its pivot.
  ///
  /// @note `pivot_x/pivot_y` are normalized anchor coordinates measured against the *trimmed* box
  /// — unlike TilesetInfo/TilePivot (tilemap.hpp), which measure pivot against the sprite's full
  /// *untrimmed* frame. Don't reuse tileset pivot-resolution logic against atlas data without
  /// accounting for this different anchor basis (see docs/isometric-fundamentals.md §7).
  struct AtlasSprite {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
    int trim_x = 0, trim_y = 0;
    int source_width = 0, source_height = 0;
    float pivot_x = 0.f, pivot_y = 0.f;
  };

  /// Top-level data parsed from a spritepacker atlas metadata JSON file (schema_version 2).
  struct SpriteAtlas {
    std::string path;
    int width = 0;
    int height = 0;
    std::vector<AtlasSprite> sprites;
  };

  /// Load and validate a sprite atlas JSON file produced by the spritepacker tool.
  /// @param path Path to the atlas metadata JSON.
  /// @return SpriteAtlas on success, or an error string on any parse or validation failure.
  [[nodiscard]] std::expected<SpriteAtlas, std::string> load_sprite_atlas(const std::filesystem::path &path);

} // namespace corundum::resources
