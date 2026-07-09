#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::gameplay::world::tilemap {

  /// Current tilemap JSON schema version. A map file with no "schema_version" field is treated as
  /// version 1 (the original, pre-versioning format). Bump this and add a step to migrate_tilemap_json
  /// (loader.cpp) whenever a schema change requires upgrading old map files at load time.
  inline constexpr int k_tilemap_schema_version = 1;

  /// Load a tileset from its JSON source file — a spritepacker atlas JSON (schema_version 2),
  /// optionally with tileset-only authoring fields layered on top (material, tile_footprints,
  /// animations; see load_tileset's doc comment in loader.cpp for the on-disk shape).
  /// @param path Path to the tileset JSON (e.g. "data/sprite_sheets/objects/terrain.json").
  /// @return TilesetInfo on success, or an error string on any parse or validation failure.
  [[nodiscard]] std::expected<TilesetInfo, std::string> load_tileset(const std::filesystem::path &path);

  /// Load and validate a tilemap from a JSON file; also loads the referenced tileset transitively.
  /// Returns a fully populated Tilemap with world-pixel collision data scaled by the tile
  /// dimensions.
  /// @param path Path to the tilemap JSON (e.g. "data/maps/world.json").
  /// @return Tilemap on success, or an error string on any schema, file-not-found, or consistency violation.
  /// @pre @p path must refer to a file whose parent directory contains the tileset source files
  ///      referenced in the map's `tilesets` array, readable relative to the current working
  ///      directory. The file must be valid JSON. The first tileset must have `first_gid` == 0 and
  ///      tilesets must be contiguous (no gaps, no overlaps) after sorting.
  /// @post On success, the returned Tilemap has all layers baked (bake_render_cache() already
  ///       called). The `tilesets` vector is sorted ascending by `first_gid`. `diamond_w()` and
  ///       `diamond_h()` return the effective isometric step dimensions, falling back to the first
  ///       tileset's first tile's full frame size if `iso_diamond_w`/`iso_diamond_h` were not set
  ///       in the JSON.
  [[nodiscard]] std::expected<Tilemap, std::string> load_tilemap(const std::filesystem::path &path);

} // namespace corundum::gameplay::world::tilemap
