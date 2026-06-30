#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::gameplay::world::tilemap {

  /// Load a tileset from its JSON source file (path, tile_width, tile_height, columns, rows,
  /// pivot, tile footprints, and animations).
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
