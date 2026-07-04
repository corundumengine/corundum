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

  /// Load a tileset from its JSON source file (path, frame_width, frame_height, columns, rows,
  /// tile footprints, and animations). Pivot defaults to (0.5, 0.0).
  /// @param path Path to the tileset JSON (e.g. "data/sprite_sheets/objects/terrain.json").
  /// @return TilesetInfo on success, or an error string on any parse or validation failure.
  [[nodiscard]] std::expected<TilesetInfo, std::string> load_tileset(const std::filesystem::path &path);

  /// Load and validate a tilemap from a JSON file; also loads the referenced tileset transitively.
  /// Returns a fully populated Tilemap with world-pixel collision data scaled by the tile
  /// dimensions.
  /// @param path Path to the tilemap JSON (e.g. "data/maps/world.json").
  /// @return Tilemap on success, or an error string on any schema, file-not-found, or consistency violation.
  [[nodiscard]] std::expected<Tilemap, std::string> load_tilemap(const std::filesystem::path &path);

} // namespace corundum::gameplay::world::tilemap
