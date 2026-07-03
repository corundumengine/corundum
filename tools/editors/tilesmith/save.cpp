#include "save.hpp"
#include "tilemap_encoding.hpp"
#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools::tilemap {

  std::expected<void, std::string> save_tilemap(EditorState &state) {
    std::ifstream in(state.map_path);
    if (!in)
      return std::unexpected("Cannot open: " + state.map_path.string());
    nlohmann::json j = nlohmann::json::parse(in);
    in.close();

    // Stamp the current schema version so re-saving an old (unversioned) map upgrades it going forward.
    j["schema_version"] = corundum::gameplay::world::tilemap::k_tilemap_schema_version;

    // Serialize tilesets from in-memory state.
    nlohmann::json tilesets_json = nlohmann::json::array();
    for (const auto &ts : state.map.tilesets) {
      nlohmann::json tj;
      tj["first_gid"] = ts.first_gid;
      tj["source"] = ts.info.source;
      tilesets_json.push_back(std::move(tj));
    }
    j["tilesets"] = std::move(tilesets_json);

    if (state.map.iso_diamond_w > 0)
      j["iso_diamond_w"] = state.map.iso_diamond_w;
    if (state.map.iso_diamond_h > 0)
      j["iso_diamond_h"] = state.map.iso_diamond_h;

    nlohmann::json layers_json = nlohmann::json::array();
    for (const auto &layer : state.map.layers) {
      std::vector<int> project_gids;
      project_gids.reserve(layer.tiles.size());
      for (corundum::gameplay::world::tilemap::TileId tid : layer.tiles)
        project_gids.push_back(static_cast<int>(tid));

      nlohmann::json lj;
      lj["name"] = layer.name;
      if (layer.z_index != 0)
        lj["z_index"] = layer.z_index;

      if (should_use_sparse(project_gids, DEFAULT_SPARSE_THRESHOLD) || !layer.animated_cells.empty() ||
          !layer.flip_flags.empty()) {
        nlohmann::json objects = convert_layer_sparse(project_gids, state.map.width, state.map.height);
        // Annotate flipped tiles
        for (auto &obj : objects) {
          const int col = obj["col"].get<int>();
          const int row = obj["row"].get<int>();
          const int idx = row * state.map.width + col;
          if (auto it = layer.flip_flags.find(idx); it != layer.flip_flags.end()) {
            const uint8_t f = it->second;
            if (f == (corundum::gameplay::world::tilemap::k_flip_h | corundum::gameplay::world::tilemap::k_flip_v))
              obj["flip"] = "HV";
            else if (f == corundum::gameplay::world::tilemap::k_flip_h)
              obj["flip"] = "H";
            else if (f == corundum::gameplay::world::tilemap::k_flip_v)
              obj["flip"] = "V";
          }
        }
        for (const auto &[idx, cell] : layer.animated_cells) {
          objects.push_back({{"col", idx % state.map.width}, {"row", idx / state.map.width}, {"anim", cell.anim_name}});
        }
        lj["objects"] = std::move(objects);
      } else {
        lj["tiles"] = convert_layer_dense(project_gids, state.map.width, state.map.height);
      }

      if (!layer.material_overrides.empty()) {
        nlohmann::json mat_overrides = nlohmann::json::array();
        for (const auto &[idx, material] : layer.material_overrides)
          mat_overrides.push_back(
              {{"col", idx % state.map.width}, {"row", idx / state.map.width}, {"material", material}});
        lj["material_overrides"] = std::move(mat_overrides);
      }

      layers_json.push_back(std::move(lj));
    }
    j["layers"] = std::move(layers_json);

    nlohmann::json collisions_json = nlohmann::json::array();
    const auto &cols = state.map.collisions;
    for (std::size_t i = 0; i < cols.size(); ++i) {
      nlohmann::json cj;
      cj["x"] = cols.cols[i];
      cj["y"] = cols.rows[i];
      cj["w"] = cols.col_spans[i];
      cj["h"] = cols.row_spans[i];
      collisions_json.push_back(std::move(cj));
    }
    j["collisions"] = std::move(collisions_json);

    using Cut = corundum::gameplay::world::tilemap::TriangleCut;
    auto cut_to_str = [](Cut c) -> const char * {
      switch (c) {
      case Cut::NW:
        return "NW";
      case Cut::NE:
        return "NE";
      case Cut::SW:
        return "SW";
      case Cut::SE:
        return "SE";
      }
      return "NW";
    };
    nlohmann::json tris_json = nlohmann::json::array();
    const auto &tris = state.map.collision_triangles;
    for (std::size_t i = 0; i < tris.size(); ++i) {
      nlohmann::json tj;
      tj["x"] = tris.cols[i];
      tj["y"] = tris.rows[i];
      tj["w"] = tris.col_spans[i];
      tj["h"] = tris.row_spans[i];
      tj["cut"] = cut_to_str(tris.cuts[i]);
      tris_json.push_back(std::move(tj));
    }
    j["collision_triangles"] = std::move(tris_json);

    {
      auto res = corundum::core::write_json(state.map_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    // Save portals alongside the tilemap.
    nlohmann::json portals_json;
    portals_json["portals"] = nlohmann::json::array();
    for (const auto &p : state.portals) {
      nlohmann::json pj;
      pj["col"] = p.col;
      pj["row"] = p.row;
      pj["w"] = p.w;
      pj["h"] = p.h;
      pj["target_map"] = p.target_map;
      pj["spawn_col"] = p.spawn_col;
      pj["spawn_row"] = p.spawn_row;
      portals_json["portals"].push_back(std::move(pj));
    }
    const auto ppath = portals_path(state.map_path);
    std::filesystem::create_directories(ppath.parent_path());
    {
      auto res = corundum::core::write_json(ppath, portals_json);
      if (!res)
        return std::unexpected(res.error());
    }

    // Write updated pivot values back to each tileset source JSON.
    for (const auto &saved_ts : state.map.tilesets) {
      if (saved_ts.info.source.empty())
        continue;
      std::ifstream tin(saved_ts.info.source);
      if (!tin)
        continue; // source file unreadable — skip silently
      nlohmann::json tj;
      try {
        tj = nlohmann::json::parse(tin);
      } catch (...) {
        continue;
      }
      tin.close();
      tj["pivot_x"] = saved_ts.info.pivot_x;
      tj["pivot_y"] = saved_ts.info.pivot_y;
      if (!saved_ts.info.tile_footprints.empty()) {
        nlohmann::json fps_json = nlohmann::json::array();
        for (const auto &[local_id, tsfp] : saved_ts.info.tile_footprints)
          fps_json.push_back({{"col", local_id % saved_ts.info.columns},
                              {"row", local_id / saved_ts.info.columns},
                              {"w", tsfp.w},
                              {"h", tsfp.h}});
        tj["tile_footprints"] = std::move(fps_json);
      } else {
        tj.erase("tile_footprints");
      }
      {
        auto res = corundum::core::write_json(saved_ts.info.source, tj);
        if (!res)
          return std::unexpected(res.error());
      }
    }

    state.dirty = false;
    return {};
  }

  std::filesystem::path portals_path(const std::filesystem::path &map_path) {
    return std::filesystem::path("data/portals") / map_path.filename();
  }

  void load_portals(EditorState &state) {
    const auto ppath = portals_path(state.map_path);
    std::ifstream in(ppath);
    if (!in) {
      state.portals.clear();
      return;
    }

    const nlohmann::json j = nlohmann::json::parse(in);
    if (!j.contains("portals") || !j["portals"].is_array())
      throw std::runtime_error("portals JSON missing 'portals' array: " + ppath.string());

    state.portals.clear();
    for (const auto &pj : j["portals"]) {
      PortalEntry e;
      e.col = pj.at("col").get<int>();
      e.row = pj.at("row").get<int>();
      e.w = pj.at("w").get<int>();
      e.h = pj.at("h").get<int>();
      e.target_map = pj.at("target_map").get<std::string>();
      e.spawn_col = pj.at("spawn_col").get<int>();
      e.spawn_row = pj.at("spawn_row").get<int>();
      state.portals.push_back(std::move(e));
    }
  }

} // namespace tools::tilemap
