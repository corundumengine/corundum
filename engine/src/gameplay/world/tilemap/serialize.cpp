#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/gameplay/world/tilemap/serialize.hpp>

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace corundum::gameplay::world::tilemap {

  // ── Layer-encoding helpers (absorbed from tools/editors/tilesmith/tilemap_encoding.hpp) ──

  namespace {

    inline constexpr int k_empty_tile_project = 0xFFFF;
    inline constexpr double k_default_sparse_threshold = 0.6;

    [[nodiscard]] bool should_use_sparse(const std::vector<int> &project_gids, double threshold) {
      if (project_gids.empty())
        return true;
      int empty_count = 0;
      for (int g : project_gids)
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
          row += std::to_string(project_gids[static_cast<std::size_t>(r * width + c)]);
        }
        rows.push_back(std::move(row));
      }
      return rows;
    }

    [[nodiscard]] json convert_layer_sparse(const std::vector<int> &project_gids, int width, int height) {
      json objects = json::array();
      for (int idx = 0; idx < width * height; ++idx) {
        const int gid = project_gids[static_cast<std::size_t>(idx)];
        if (gid == k_empty_tile_project)
          continue;
        objects.push_back({{"col", idx % width}, {"row", idx / width}, {"id", gid}});
      }
      return objects;
    }

    [[nodiscard]] const char *cut_to_str(TriangleCut c) {
      switch (c) {
      case TriangleCut::NW:
        return "NW";
      case TriangleCut::NE:
        return "NE";
      case TriangleCut::SW:
        return "SW";
      case TriangleCut::SE:
        return "SE";
      }
      std::unreachable();
    }

  } // namespace

  // ── Public API ─────────────────────────────────────────────────────────────────

  json serialize_tilemap(const Tilemap &map, const json *base) {
    json j = base ? *base : json::object();

    j["schema_version"] = k_tilemap_schema_version;

    // Tilesets
    {
      json tilesets_json = json::array();
      for (const auto &ts : map.tilesets)
        tilesets_json.push_back({{"first_gid", ts.first_gid}, {"source", ts.info.source}});
      j["tilesets"] = std::move(tilesets_json);
    }

    if (map.iso_diamond_w > 0)
      j["iso_diamond_w"] = map.iso_diamond_w;
    if (map.iso_diamond_h > 0)
      j["iso_diamond_h"] = map.iso_diamond_h;

    // Layers
    {
      json layers_json = json::array();
      for (const auto &layer : map.layers) {
        std::vector<int> project_gids;
        project_gids.reserve(layer.tiles.size());
        for (TileId tid : layer.tiles)
          project_gids.push_back(static_cast<int>(tid));

        json lj;
        lj["name"] = layer.name;
        if (layer.z_index != 0)
          lj["z_index"] = layer.z_index;

        if (should_use_sparse(project_gids, k_default_sparse_threshold) || !layer.animated_cells.empty() ||
            !layer.flip_flags.empty()) {
          json objects = convert_layer_sparse(project_gids, map.width, map.height);
          for (auto &obj : objects) {
            const int col = obj["col"].get<int>();
            const int row = obj["row"].get<int>();
            const int idx = row * map.width + col;
            if (auto it = layer.flip_flags.find(idx); it != layer.flip_flags.end()) {
              const uint8_t f = it->second;
              if (f == (k_flip_h | k_flip_v))
                obj["flip"] = "HV";
              else if (f == k_flip_h)
                obj["flip"] = "H";
              else if (f == k_flip_v)
                obj["flip"] = "V";
            }
          }
          for (const auto &[idx, cell] : layer.animated_cells)
            objects.push_back({{"col", idx % map.width}, {"row", idx / map.width}, {"anim", cell.anim_name}});
          lj["objects"] = std::move(objects);
        } else {
          lj["tiles"] = convert_layer_dense(project_gids, map.width, map.height);
        }

        if (!layer.elevation.empty()) {
          const std::vector<int> elev_ints(layer.elevation.begin(), layer.elevation.end());
          lj["elevation"] = convert_layer_dense(elev_ints, map.width, map.height);
        }

        if (!layer.material_overrides.empty()) {
          json mat_overrides = json::array();
          for (const auto &[idx, material] : layer.material_overrides)
            mat_overrides.push_back({{"col", idx % map.width}, {"row", idx / map.width}, {"material", material}});
          lj["material_overrides"] = std::move(mat_overrides);
        }

        if (!layer.ramps.empty()) {
          json ramps_json = json::array();
          for (const auto &[idx, axis] : layer.ramps) {
            const char *axis_str = axis == RampAxis::NORTH_SOUTH ? "ns" : "ew";
            ramps_json.push_back({{"col", idx % map.width}, {"row", idx / map.width}, {"axis", axis_str}});
          }
          lj["ramps"] = std::move(ramps_json);
        }

        layers_json.push_back(std::move(lj));
      }
      j["layers"] = std::move(layers_json);
    }

    // Collisions
    {
      json collisions_json = json::array();
      const std::size_t n = map.collisions.cols.size();
      for (std::size_t i = 0; i < n; ++i)
        collisions_json.push_back({{"x", map.collisions.cols[i]},
                                   {"y", map.collisions.rows[i]},
                                   {"w", map.collisions.col_spans[i]},
                                   {"h", map.collisions.row_spans[i]},
                                   {"elevation", map.collisions.elevations[i]}});
      j["collisions"] = std::move(collisions_json);
    }

    // Collision triangles
    {
      json tris_json = json::array();
      const std::size_t n = map.collision_triangles.cols.size();
      for (std::size_t i = 0; i < n; ++i)
        tris_json.push_back({{"x", map.collision_triangles.cols[i]},
                             {"y", map.collision_triangles.rows[i]},
                             {"w", map.collision_triangles.col_spans[i]},
                             {"h", map.collision_triangles.row_spans[i]},
                             {"cut", cut_to_str(map.collision_triangles.cuts[i])},
                             {"elevation", map.collision_triangles.elevations[i]}});
      j["collision_triangles"] = std::move(tris_json);
    }

    return j;
  }

  json serialize_tiledata(const TilesetInfo &info) {
    json sc;
    sc["schema_version"] = 1;

    if (!info.tile_footprints.empty()) {
      json fps_json = json::array();
      for (const auto &[local_id, tsfp] : info.tile_footprints) {
        if (local_id < 0 || static_cast<std::size_t>(local_id) >= info.tile_names.size())
          continue;
        fps_json.push_back(
            {{"name", info.tile_names[static_cast<std::size_t>(local_id)]}, {"w", tsfp.w}, {"h", tsfp.h}});
      }
      sc["tile_footprints"] = std::move(fps_json);
    }

    {
      json pivots = json::array();
      for (std::size_t i = 0; i < info.tile_names.size(); ++i) {
        json pe;
        pe["name"] = info.tile_names[i];
        pe["pivot_x"] = info.tile_pivot_x[i];
        pe["pivot_y"] = info.tile_pivot_y[i];
        pivots.push_back(pe);
      }
      sc["pivots"] = std::move(pivots);
    }

    return sc;
  }

} // namespace corundum::gameplay::world::tilemap
