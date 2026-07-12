#pragma once
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <nlohmann/json.hpp>

namespace corundum::gameplay::world::tilemap {

  /** @brief Serialize a Tilemap to JSON.
   *
   * When @p base is non-null, the result starts as a copy of @p base and only
   * the fields below are overwritten. Unknown keys present in @p base are
   * preserved in the output. When @p base is null, a fresh JSON object is
   * produced containing only the engine-managed fields.
   *
   * Fields written: schema_version, tilesets, iso_diamond_w, iso_diamond_h,
   * layers (name, z_index, tiles/objects, elevation, material_overrides,
   * ramps), collisions, collision_triangles.
   *
   * @param[in] map   The in-memory tilemap to serialize.
   * @param[in] base  Optional existing JSON to merge onto (preserves unknown keys).
   * @return JSON object suitable for write_json().
   */
  [[nodiscard]] nlohmann::json serialize_tilemap(const Tilemap &map, const nlohmann::json *base = nullptr);

  /** @brief Serialize tileset authoring data to a tiledata sidecar JSON.
   *
   * Produces the same format that read_sidecar() (loader.cpp) parses:
   * schema_version: 1, optional tile_footprints array, and pivot array for
   * every tile.
   *
   * @param[in] info  A fully-loaded TilesetInfo (with footprints and pivots).
   * @return JSON object suitable for write_json() to a .tiledata.json sidecar.
   */
  [[nodiscard]] nlohmann::json serialize_tiledata(const TilesetInfo &info);

} // namespace corundum::gameplay::world::tilemap
