// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "encoding.hpp"

#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/serialize.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::world::tilemap {

  namespace {

    constexpr int k_empty_tile_project = 0xFFFF;
    constexpr double k_default_sparse_threshold = 0.6;

    [[nodiscard]] bool should_use_sparse(const std::vector<int> &project_gids, double threshold) {
      if (project_gids.empty())
        return true;
      int empty_count = 0;
      for (const int g : project_gids)
        if (g == k_empty_tile_project)
          ++empty_count;
      return (static_cast<double>(empty_count) / static_cast<double>(project_gids.size())) > threshold;
    }

    [[nodiscard]] std::vector<std::string> convert_layer_dense(const std::vector<int> &project_gids, int width,
                                                               int height) {
      std::vector<std::string> rows;
      rows.reserve(static_cast<std::size_t>(height));
      for (int r = 0; r < height; ++r) {
        std::string row;
        for (int c = 0; c < width; ++c) {
          if (c > 0)
            row += ',';
          const std::size_t index =
              (static_cast<std::size_t>(r) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(c);
          row += std::to_string(project_gids[index]);
        }
        rows.push_back(std::move(row));
      }
      return rows;
    }

    [[nodiscard]] json convert_layer_sparse(const std::vector<int> &project_gids, int width, int height) {
      json objects = json::array();
      const auto col_count = static_cast<std::size_t>(width);
      const std::size_t cell_count = col_count * static_cast<std::size_t>(height);
      for (std::size_t idx = 0; idx < cell_count; ++idx) {
        const int gid = project_gids[idx];
        if (gid == k_empty_tile_project)
          continue;
        objects.push_back(
            {{"col", static_cast<int>(idx % col_count)}, {"row", static_cast<int>(idx / col_count)}, {"id", gid}});
      }
      return objects;
    }

    [[nodiscard]] json build_sparse_objects(const TilemapLayer &layer, const std::vector<int> &project_gids, int width,
                                            int height) {
      json objects = convert_layer_sparse(project_gids, width, height);
      for (auto &obj : objects) {
        const int col = obj["col"].get<int>();
        const int row = obj["row"].get<int>();
        const int idx = (row * width) + col;
        if (const auto it = layer.flip_flags.find(idx); it != layer.flip_flags.end()) {
          const std::string_view flip = to_string(it->second);
          if (!flip.empty())
            obj["flip"] = flip;
        }
      }
      for (const auto &[idx, cell] : layer.animated_cells)
        objects.push_back({{"col", idx % width}, {"row", idx / width}, {"anim", cell.anim_name}});
      return objects;
    }

    [[nodiscard]] json serialize_material_overrides(const TilemapLayer &layer, int width) {
      json overrides = json::array();
      for (const auto &[idx, material] : layer.material_overrides)
        overrides.push_back({{"col", idx % width}, {"row", idx / width}, {"material", material}});
      return overrides;
    }

    [[nodiscard]] json serialize_ramps(const TilemapLayer &layer, int width) {
      json ramps = json::array();
      for (const auto &[idx, axis] : layer.ramps)
        ramps.push_back({{"col", idx % width}, {"row", idx / width}, {"axis", std::string{to_string(axis)}}});
      return ramps;
    }

    [[nodiscard]] json serialize_layer(const TilemapLayer &layer, int width, int height) {
      std::vector<int> project_gids;
      project_gids.reserve(layer.tiles.size());
      for (const TileId tid : layer.tiles)
        project_gids.push_back(static_cast<int>(tid));

      json layer_json;
      layer_json["name"] = layer.name;
      if (layer.z_index != 0)
        layer_json["z_index"] = layer.z_index;
      if (layer.depth_sorted)
        layer_json["depth_sorted"] = layer.depth_sorted;

      if (should_use_sparse(project_gids, k_default_sparse_threshold) || !layer.animated_cells.empty() ||
          !layer.flip_flags.empty()) {
        layer_json["objects"] = build_sparse_objects(layer, project_gids, width, height);
      } else {
        layer_json["tiles"] = convert_layer_dense(project_gids, width, height);
      }

      if (!layer.elevation.empty()) {
        const std::vector<int> elev_ints(layer.elevation.begin(), layer.elevation.end());
        layer_json["elevation"] = convert_layer_dense(elev_ints, width, height);
      }
      if (!layer.material_overrides.empty())
        layer_json["material_overrides"] = serialize_material_overrides(layer, width);
      if (!layer.ramps.empty())
        layer_json["ramps"] = serialize_ramps(layer, width);

      return layer_json;
    }

    [[nodiscard]] json serialize_tilesets(const std::vector<TilemapTileset> &tilesets) {
      json out = json::array();
      for (const auto &tileset : tilesets)
        out.push_back({{"first_gid", tileset.first_gid}, {"source", tileset.info.source}});
      return out;
    }

    [[nodiscard]] json serialize_collisions(const CollisionRects &collisions) {
      json out = json::array();
      for (std::size_t i = 0; i < collisions.cols.size(); ++i)
        out.push_back({
            {"x", collisions.cols[i]},
            {"y", collisions.rows[i]},
            {"w", collisions.col_spans[i]},
            {"h", collisions.row_spans[i]},
            {"elevation", collisions.elevations[i]},
        });
      return out;
    }

    [[nodiscard]] json serialize_collision_triangles(const CollisionTriangles &triangles) {
      json out = json::array();
      for (std::size_t i = 0; i < triangles.cols.size(); ++i)
        out.push_back({
            {"x", triangles.cols[i]},
            {"y", triangles.rows[i]},
            {"w", triangles.col_spans[i]},
            {"h", triangles.row_spans[i]},
            {"cut", std::string{to_string(triangles.cuts[i])}},
            {"elevation", triangles.elevations[i]},
        });
      return out;
    }

  } // namespace

  json serialize(const Tilemap &map, const json *base) {
    json j = base != nullptr ? *base : json::object();

    j["schema_version"] = k_tilemap_schema_version;
    j["tilesets"] = serialize_tilesets(map.tilesets);

    if (map.iso_diamond_w > 0)
      j["iso_diamond_w"] = map.iso_diamond_w;
    if (map.iso_diamond_h > 0)
      j["iso_diamond_h"] = map.iso_diamond_h;

    json layers = json::array();
    for (const auto &layer : map.layers)
      layers.push_back(serialize_layer(layer, map.width, map.height));
    j["layers"] = std::move(layers);

    j["collisions"] = serialize_collisions(map.collisions);
    j["collision_triangles"] = serialize_collision_triangles(map.collision_triangles);

    return j;
  }

} // namespace corundum::world::tilemap
