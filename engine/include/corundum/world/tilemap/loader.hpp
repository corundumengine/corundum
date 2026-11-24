// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace corundum::core {
class ContentSource;
}

namespace corundum::world::tilemap {

/// Current tilemap JSON schema version. A map file with no "schema_version" field is treated as
/// version 1 (the original, pre-versioning format). Pre-1.0, bump this whenever a schema change
/// would break old map files; older versions are rejected at load time rather than migrated.
constexpr int k_tilemap_schema_version = 1;

/// Load a tileset from its JSON source file — a spritepacker atlas JSON (schema_version 2),
/// optionally with tileset-only authoring fields layered on top (material, animations; see
/// load_tileset's doc comment in loader.cpp for the on-disk shape).
/// Reads the file on every call; TilemapLoader keeps tilesets for reuse.
/// @param path Path to the tileset JSON (e.g. "data/sprite_sheets/objects/terrain.json").
/// @return TilesetInfo on success, or an error string on any parse or validation failure.
[[nodiscard]] std::expected<TilesetInfo, std::string> load_tileset(const core::ContentSource &content,
                                                                   const std::filesystem::path &path);

/**
 * @brief Loads tilemaps from one content source and keeps every tileset it resolves, keyed by
 * path, so maps that share a tileset read and resolve it once.
 *
 * Streamed-world chunks share a handful of tilesets; resolving them again for every chunk was
 * the dominant chunk-streaming cost (~43 ms per 16x16 chunk).
 *
 * A kept tileset is never re-read, so a change to its file reaches only a new loader.
 * @note Not thread-safe. Call only from the main thread.
 */
class TilemapLoader {
public:
  /// @pre @p content outlives the loader.
  explicit TilemapLoader(const core::ContentSource &content) : content_(&content) {}

  /// Same contract as the free load_tilemap().
  [[nodiscard]] std::expected<Tilemap, std::string> load_tilemap(const std::filesystem::path &path);

  /// Same contract as the free load_tileset().
  [[nodiscard]] std::expected<TilesetInfo, std::string> load_tileset(const std::filesystem::path &path);

  /// The source every load reads from.
  [[nodiscard]] const core::ContentSource &content() const { return *content_; }

private:
  const core::ContentSource *content_;

  std::unordered_map<std::string, TilesetInfo> tilesets_by_path_;
};

/// Load and validate a tilemap from a JSON file; also loads the referenced tileset transitively,
/// reading each tileset file afresh.
/// Returns a fully populated Tilemap with world-pixel collision data scaled by the tile
/// dimensions.
/// @param path Path to the tilemap JSON (e.g. "data/maps/world.json").
/// @return Tilemap on success, or an error string on any schema, file-not-found, or consistency violation.
/// @pre @p path must refer to a readable, valid-JSON file. Tileset `source` paths are resolved
///      relative to the current working directory, so they must be reachable from there.
/// @post The `tilesets` vector is sorted ascending by `first_gid`. `diamond_w()` and
///       `diamond_h()` return the effective isometric step dimensions, falling back to the first
///       tileset's first tile's full frame size if `iso_diamond_w`/`iso_diamond_h` were not set
///       in the JSON.
[[nodiscard]] std::expected<Tilemap, std::string> load_tilemap(const core::ContentSource &content,
                                                               const std::filesystem::path &path);

} // namespace corundum::world::tilemap
