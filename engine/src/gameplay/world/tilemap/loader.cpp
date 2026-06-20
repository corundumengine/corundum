#include <algorithm>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <flat_map>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::gameplay::world::tilemap {

  namespace {

    std::expected<TilesetInfo, std::string> load_tileset(const fs::path &tileset_path) {
      std::ifstream f(tileset_path);
      if (!f)
        return std::unexpected(std::format("Cannot open tileset: {}", tileset_path.string()));

      json j;
      try {
        j = json::parse(f);
      } catch (const json::exception &e) {
        return std::unexpected(std::format("Malformed tileset {}: {}", tileset_path.string(), e.what()));
      }

      auto require_str = [&](const char *key) -> std::expected<std::string, std::string> {
        if (!j.contains(key))
          return std::unexpected(std::format("Tileset '{}' missing '{}'", tileset_path.string(), key));
        try {
          return j[key].get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tileset field '{}' has wrong type", key));
        }
      };

      auto require_pos_int = [&](const char *key) -> std::expected<int, std::string> {
        if (!j.contains(key))
          return std::unexpected(std::format("Tileset '{}' missing '{}'", tileset_path.string(), key));
        int v;
        try {
          v = j[key].get<int>();
        } catch (...) {
          return std::unexpected(std::format("Tileset field '{}' has wrong type", key));
        }
        if (v <= 0)
          return std::unexpected(std::format("Tileset field '{}' must be positive", key));
        return v;
      };

      TilesetInfo info;
      info.source = tileset_path.string();

      {
        auto r = require_str("path");
        if (!r)
          return std::unexpected(std::move(r.error()));
        info.path = std::move(*r);
      }
      {
        auto r = require_pos_int("tile_width");
        if (!r)
          return std::unexpected(std::move(r.error()));
        info.tile_width = *r;
      }
      {
        auto r = require_pos_int("tile_height");
        if (!r)
          return std::unexpected(std::move(r.error()));
        info.tile_height = *r;
      }
      {
        auto r = require_pos_int("columns");
        if (!r)
          return std::unexpected(std::move(r.error()));
        info.columns = *r;
      }
      {
        auto r = require_pos_int("rows");
        if (!r)
          return std::unexpected(std::move(r.error()));
        info.rows = *r;
      }
      if (j.contains("pivot_x")) {
        try {
          info.pivot_x = j["pivot_x"].get<float>();
        } catch (...) {
        }
      }
      if (j.contains("pivot_y")) {
        try {
          info.pivot_y = j["pivot_y"].get<float>();
        } catch (...) {
        }
      }
      if (j.contains("tile_footprints") && j["tile_footprints"].is_array()) {
        const auto &fps_json = j["tile_footprints"];
        for (std::size_t fi = 0; fi < fps_json.size(); ++fi) {
          const auto &entry = fps_json[fi];
          if (!entry.is_object())
            continue;
          int col = 0, row = 0, w = 1, h = 1;
          try {
            col = entry.at("col").get<int>();
            row = entry.at("row").get<int>();
          } catch (...) {
            return std::unexpected(
                std::format("Tileset '{}' tile_footprints[{}] missing 'col' or 'row'", tileset_path.string(), fi));
          }
          if (col < 0 || col >= info.columns || row < 0 || row >= info.rows)
            return std::unexpected(
                std::format("Tileset '{}' tile_footprints[{}] col/row out of range", tileset_path.string(), fi));
          if (entry.contains("w")) {
            try {
              w = entry["w"].get<int>();
            } catch (...) {
            }
          }
          if (entry.contains("h")) {
            try {
              h = entry["h"].get<int>();
            } catch (...) {
            }
          }
          info.tile_footprints[row * info.columns + col] = {std::max(1, w), std::max(1, h)};
        }
      }

      if (j.contains("animations")) {
        const auto &anim_obj = j["animations"];
        if (!anim_obj.is_object())
          return std::unexpected(
              std::format("Tileset '{}' field 'animations' must be an object", tileset_path.string()));

        float default_fps = 5.f;
        if (anim_obj.contains("fps")) {
          try {
            default_fps = anim_obj["fps"].get<float>();
          } catch (...) {
            return std::unexpected(std::format("Tileset '{}' animations 'fps' has wrong type", tileset_path.string()));
          }
          if (default_fps <= 0.f)
            return std::unexpected(
                std::format("Tileset '{}' animations 'fps' must be positive", tileset_path.string()));
        }

        if (!anim_obj.contains("clips") || !anim_obj["clips"].is_array())
          return std::unexpected(std::format("Tileset '{}' animations missing 'clips' array", tileset_path.string()));

        const auto &clips = anim_obj["clips"];
        for (std::size_t ci = 0; ci < clips.size(); ++ci) {
          const auto &clip = clips[ci];
          if (!clip.is_object())
            return std::unexpected(
                std::format("Tileset '{}' animations.clips[{}] must be an object", tileset_path.string(), ci));

          std::string name;
          try {
            name = clip.at("name").get<std::string>();
          } catch (...) {
            return std::unexpected(
                std::format("Tileset '{}' animations.clips[{}] missing or invalid 'name'", tileset_path.string(), ci));
          }
          if (name.empty())
            return std::unexpected(
                std::format("Tileset '{}' animations.clips[{}] 'name' must not be empty", tileset_path.string(), ci));

          if (!clip.contains("frames") || !clip["frames"].is_array())
            return std::unexpected(
                std::format("Tileset '{}' animations.clips[{}] missing 'frames' array", tileset_path.string(), ci));

          TileAnimation anim;
          anim.fps = default_fps;
          if (clip.contains("fps")) {
            try {
              anim.fps = clip["fps"].get<float>();
            } catch (...) {
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] 'fps' has wrong type", tileset_path.string(), ci));
            }
            if (anim.fps <= 0.f)
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] 'fps' must be positive", tileset_path.string(), ci));
          }

          for (std::size_t fi = 0; fi < clip["frames"].size(); ++fi) {
            const auto &frame = clip["frames"][fi];
            if (!frame.is_object())
              return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] must "
                                                 "be an object with 'col' and 'row'",
                                                 tileset_path.string(), ci, fi));
            int col, row;
            try {
              col = frame.at("col").get<int>();
              row = frame.at("row").get<int>();
            } catch (...) {
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] frames[{}] missing or invalid 'col' or 'row'",
                              tileset_path.string(), ci, fi));
            }
            if (col < 0 || col >= info.columns)
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] frames[{}] col={} out of range [0, {})",
                              tileset_path.string(), ci, fi, col, info.columns));
            if (row < 0 || row >= info.rows)
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] frames[{}] row={} out of range [0, {})",
                              tileset_path.string(), ci, fi, row, info.rows));
            anim.frames.push_back(row * info.columns + col);
          }
          info.animations[name] = std::move(anim);
        }
      }

      return info;
    }

  } // namespace

  std::expected<Tilemap, std::string> load_tilemap(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open tilemap: {}", path.string()));

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed tilemap {}: {}", path.string(), e.what()));
    }

    // id
    if (!j.contains("id"))
      return std::unexpected(std::format("Tilemap '{}' missing 'id'", path.string()));
    std::string id;
    try {
      id = j["id"].get<std::string>();
    } catch (...) {
      return std::unexpected(std::string{"Tilemap 'id' has wrong type"});
    }
    if (id.empty())
      return std::unexpected(std::string{"Tilemap 'id' must not be empty"});

    // collisions (optional)
    CollisionRects collisions;
    if (j.contains("collisions")) {
      if (!j["collisions"].is_array())
        return std::unexpected(std::format("Tilemap '{}' field 'collisions' must be an array", id));
      const auto &col_json = j["collisions"];
      collisions.xs.reserve(col_json.size());
      collisions.ys.reserve(col_json.size());
      collisions.ws.reserve(col_json.size());
      collisions.hs.reserve(col_json.size());
      for (std::size_t ci = 0; ci < col_json.size(); ++ci) {
        const auto &entry = col_json[ci];
        if (!entry.is_object())
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] must be an object", id, ci));
        CollisionRect r;
        try {
          r.x = entry.at("x").get<float>();
          r.y = entry.at("y").get<float>();
          r.w = entry.at("w").get<float>();
          r.h = entry.at("h").get<float>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' collisions[{}] missing or invalid field (x, y, w, h required)", id, ci));
        }
        if (r.w <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] 'w' must be positive", id, ci));
        if (r.h <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] 'h' must be positive", id, ci));
        collisions.push_back(r.x, r.y, r.w, r.h);
      }
    }

    // collision_triangles (optional)
    CollisionTriangles collision_triangles;
    if (j.contains("collision_triangles")) {
      if (!j["collision_triangles"].is_array())
        return std::unexpected(std::format("Tilemap '{}' field 'collision_triangles' must be an array", id));
      const auto &tri_json = j["collision_triangles"];
      collision_triangles.xs.reserve(tri_json.size());
      collision_triangles.ys.reserve(tri_json.size());
      collision_triangles.ws.reserve(tri_json.size());
      collision_triangles.hs.reserve(tri_json.size());
      collision_triangles.cuts.reserve(tri_json.size());
      for (std::size_t ci = 0; ci < tri_json.size(); ++ci) {
        const auto &entry = tri_json[ci];
        if (!entry.is_object())
          return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] must be an object", id, ci));
        float x, y, w, h;
        std::string cut_str;
        try {
          x = entry.at("x").get<float>();
          y = entry.at("y").get<float>();
          w = entry.at("w").get<float>();
          h = entry.at("h").get<float>();
          cut_str = entry.at("cut").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] missing or "
                                             "invalid field (x, y, w, h, cut required)",
                                             id, ci));
        }
        if (w <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] 'w' must be positive", id, ci));
        if (h <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] 'h' must be positive", id, ci));
        TriangleCut cut;
        if (cut_str == "NW")
          cut = TriangleCut::NW;
        else if (cut_str == "NE")
          cut = TriangleCut::NE;
        else if (cut_str == "SW")
          cut = TriangleCut::SW;
        else if (cut_str == "SE")
          cut = TriangleCut::SE;
        else
          return std::unexpected(
              std::format("Tilemap '{}' collision_triangles[{}] 'cut' must be NW, NE, SW, or SE", id, ci));
        collision_triangles.push_back(x, y, w, h, cut);
      }
    }

    // tilesets array
    if (!j.contains("tilesets") || !j["tilesets"].is_array())
      return std::unexpected(std::format("Tilemap '{}' missing 'tilesets' array", id));
    const auto &tilesets_json = j["tilesets"];
    if (tilesets_json.empty())
      return std::unexpected(std::format("Tilemap '{}' 'tilesets' must not be empty", id));

    std::vector<TilemapTileset> tilesets;
    tilesets.reserve(tilesets_json.size());
    for (std::size_t ti = 0; ti < tilesets_json.size(); ++ti) {
      const auto &entry = tilesets_json[ti];
      if (!entry.is_object())
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] must be an object", id, ti));
      int first_gid;
      std::string source;
      try {
        first_gid = entry.at("first_gid").get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] missing or invalid 'first_gid'", id, ti));
      }
      if (first_gid < 0)
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'first_gid' must be >= 0", id, ti));
      if (first_gid > static_cast<int>(k_empty_tile) - 1)
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'first_gid'={} exceeds maximum {}", id, ti,
                                           first_gid, static_cast<int>(k_empty_tile) - 1));
      try {
        source = entry.at("source").get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] missing or invalid 'source'", id, ti));
      }
      if (source.empty())
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'source' must not be empty", id, ti));
      TilesetInfo info;
      auto tileset_result = load_tileset(fs::path(source));
      if (!tileset_result.has_value())
        return std::unexpected(std::format("[{}] tilesets[{}]: {}", id, ti, tileset_result.error()));
      info = std::move(*tileset_result);
      TilemapTileset ts;
      ts.info = std::move(info);
      ts.first_gid = static_cast<TileId>(first_gid);
      ts.tile_count = ts.info.columns * ts.info.rows;
      if (static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.tile_count) >
          static_cast<uint32_t>(k_empty_tile))
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] GID range [{}, {}) exceeds reserved sentinel {}",
                                           id, ti, ts.first_gid,
                                           static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.tile_count),
                                           static_cast<int>(k_empty_tile)));
      tilesets.push_back(std::move(ts));
    }

    // Sort ascending by first_gid.
    std::ranges::sort(tilesets,
                      [](const TilemapTileset &a, const TilemapTileset &b) { return a.first_gid < b.first_gid; });

    // first entry must have first_gid == 0.
    if (tilesets[0].first_gid != 0)
      return std::unexpected(std::format("Tilemap '{}' first tileset must have first_gid=0", id));

    // No duplicates; strict contiguity (no gaps, no overlaps).
    for (std::size_t i = 0; i + 1 < tilesets.size(); ++i) {
      const TileId expected_next = tilesets[i].first_gid + static_cast<TileId>(tilesets[i].tile_count);
      if (tilesets[i].first_gid == tilesets[i + 1].first_gid)
        return std::unexpected(
            std::format("Tilemap '{}' tilesets have duplicate first_gid={}", id, tilesets[i].first_gid));
      if (tilesets[i + 1].first_gid != expected_next)
        return std::unexpected(
            std::format("Tilemap '{}' tilesets are not contiguous: gap or overlap between first_gid={} "
                        "(tile_count={}) and first_gid={})",
                        id, tilesets[i].first_gid, tilesets[i].tile_count, tilesets[i + 1].first_gid));
    }

    // width, height
    auto require_pos_int = [&](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Tilemap '{}' missing '{}'", id, key));
      int v;
      try {
        v = j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' field '{}' has wrong type", id, key));
      }
      if (v <= 0)
        return std::unexpected(std::format("Tilemap '{}' field '{}' must be positive", id, key));
      return v;
    };

    int width = 0;
    {
      auto r = require_pos_int("width");
      if (!r)
        return std::unexpected(std::move(r.error()));
      width = *r;
    }
    int height = 0;
    {
      auto r = require_pos_int("height");
      if (!r)
        return std::unexpected(std::move(r.error()));
      height = *r;
    }

    int iso_diamond_w = 0, iso_diamond_h = 0;
    if (j.contains("iso_diamond_w")) {
      try {
        iso_diamond_w = j["iso_diamond_w"].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' field 'iso_diamond_w' has wrong type (expected integer)", id));
      }
      if (iso_diamond_w <= 0)
        return std::unexpected(std::format("Tilemap '{}' field 'iso_diamond_w' must be positive", id));
    }
    if (j.contains("iso_diamond_h")) {
      try {
        iso_diamond_h = j["iso_diamond_h"].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' field 'iso_diamond_h' has wrong type (expected integer)", id));
      }
      if (iso_diamond_h <= 0)
        return std::unexpected(std::format("Tilemap '{}' field 'iso_diamond_h' must be positive", id));
    }

    // layers array
    if (!j.contains("layers") || !j["layers"].is_array())
      return std::unexpected(std::format("Tilemap '{}' missing 'layers' array", id));

    const auto &layers_json = j["layers"];
    if (layers_json.empty())
      return std::unexpected(std::format("Tilemap '{}' must have at least one layer", id));

    const int expected = width * height;

    // Dense format: array of height strings, each a comma-separated row of width tile IDs.
    auto parse_dense = [&](const json &layer_json,
                           const std::string &layer_name) -> std::expected<std::vector<TileId>, std::string> {
      const auto &tiles_json = layer_json["tiles"];
      if (static_cast<int>(tiles_json.size()) != height)
        return std::unexpected(std::format("Tilemap '{}' layer '{}' tiles row count mismatch: expected {}, got {}", id,
                                           layer_name, height, tiles_json.size()));

      std::vector<TileId> tiles;
      tiles.reserve(static_cast<std::size_t>(expected));

      for (int r = 0; r < height; ++r) {
        std::string row_str;
        try {
          row_str = tiles_json[r].get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tilemap '{}' layer '{}' tiles[{}] must be a string", id, layer_name, r));
        }

        std::istringstream ss(row_str);
        std::string token;
        int col = 0;
        while (std::getline(ss, token, ',')) {
          if (col >= width)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} has more than {} tiles", id, layer_name, r, width));
          int v;
          try {
            v = std::stoi(token);
          } catch (...) {
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} col {}: not an integer", id, layer_name, r, col));
          }
          if (v < 0)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} col {}: id={} is negative", id, layer_name, r, col, v));
          if (v != static_cast<int>(k_empty_tile) && find_tileset(tilesets, static_cast<TileId>(v)) == nullptr)
            return std::unexpected(std::format(
                "Tilemap '{}' layer '{}' row {} col {}: GID={} not covered by any tileset", id, layer_name, r, col, v));
          tiles.push_back(static_cast<TileId>(v));
          ++col;
        }
        if (col != width)
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' row {} has {} tiles, expected {}", id, layer_name, r, col, width));
      }
      return tiles;
    };

    using TilesAndAnims =
        std::tuple<std::vector<TileId>, std::flat_map<int, AnimatedCell>, std::flat_map<int, uint8_t>>;

    auto parse_sparse = [&](const json &layer_json,
                            const std::string &layer_name) -> std::expected<TilesAndAnims, std::string> {
      std::vector<TileId> tiles(static_cast<std::size_t>(expected), k_empty_tile);
      std::vector<bool> occupied(static_cast<std::size_t>(expected), false);
      std::flat_map<int, AnimatedCell> animated_cells;
      std::flat_map<int, uint8_t> flip_flags_map;

      for (std::size_t i = 0; i < layer_json["objects"].size(); ++i) {
        const auto &entry = layer_json["objects"][i];
        if (!entry.is_object())
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' objects[{}] must be an object", id, layer_name, i));

        const bool has_id = entry.contains("id");
        const bool has_anim = entry.contains("anim");
        if (has_id && has_anim)
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' objects[{}] must have 'id' or 'anim', not both", id, layer_name, i));
        if (!has_id && !has_anim)
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' objects[{}] missing 'id' or 'anim'", id, layer_name, i));

        int col, row;
        try {
          col = entry.at("col").get<int>();
          row = entry.at("row").get<int>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' objects[{}] missing or invalid 'col' or 'row'", id, layer_name, i));
        }
        if (col < 0 || col >= width || row < 0 || row >= height)
          return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] position ({}, {}) out of bounds", id,
                                             layer_name, i, col, row));

        const auto idx = static_cast<std::size_t>(row * width + col);
        if (occupied[idx])
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' duplicate entry at ({}, {})", id, layer_name, col, row));
        occupied[idx] = true;

        uint8_t flip = 0;
        if (!has_anim && entry.contains("flip")) {
          std::string flip_str;
          try {
            flip_str = entry.at("flip").get<std::string>();
          } catch (...) {
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' objects[{}] 'flip' must be a string", id, layer_name, i));
          }
          if (flip_str == "H")
            flip = k_flip_h;
          else if (flip_str == "V")
            flip = k_flip_v;
          else if (flip_str == "HV")
            flip = k_flip_h | k_flip_v;
          else
            return std::unexpected(std::format(
                "Tilemap '{}' layer '{}' objects[{}] 'flip' must be \"H\", \"V\", or \"HV\"", id, layer_name, i));
        }

        if (has_anim) {
          std::string anim_name;
          try {
            anim_name = entry.at("anim").get<std::string>();
          } catch (...) {
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' objects[{}] 'anim' must be a string", id, layer_name, i));
          }
          // Resolve animation name across all tilesets.
          AnimatedCell cell;
          bool found = false;
          for (const auto &ts : tilesets) {
            auto it = ts.info.animations.find(anim_name);
            if (it == ts.info.animations.end())
              continue;
            const auto &ta = it->second;
            cell.anim_name = anim_name;
            cell.fps = ta.fps;
            cell.frame_gids.reserve(ta.frames.size());
            for (int local_id : ta.frames)
              cell.frame_gids.push_back(ts.first_gid + static_cast<TileId>(local_id));
            found = true;
            break;
          }
          if (!found)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' objects[{}] animation '{}' not found in any tileset", id,
                            layer_name, i, anim_name));
          // tiles[idx] stays k_empty_tile; the renderer uses animated_cells instead.
          animated_cells[static_cast<int>(idx)] = std::move(cell);
        } else {
          int tile_id;
          try {
            tile_id = entry.at("id").get<int>();
          } catch (...) {
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' objects[{}] missing or invalid 'id'", id, layer_name, i));
          }
          if (tile_id < 0 || find_tileset(tilesets, static_cast<TileId>(tile_id)) == nullptr)
            return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] GID={} not covered by any tileset",
                                               id, layer_name, i, tile_id));
          tiles[idx] = static_cast<TileId>(tile_id);
          if (flip != 0)
            flip_flags_map[static_cast<int>(idx)] = flip;
        }
      }
      return std::make_tuple(std::move(tiles), std::move(animated_cells), std::move(flip_flags_map));
    };

    auto parse_tiles = [&](const json &layer_json,
                           const std::string &layer_name) -> std::expected<TilesAndAnims, std::string> {
      const bool has_dense = layer_json.contains("tiles") && layer_json["tiles"].is_array();
      const bool has_sparse = layer_json.contains("objects") && layer_json["objects"].is_array();
      if (has_dense && has_sparse)
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' must have 'tiles' or 'objects', not both", id, layer_name));
      if (has_dense) {
        auto r = parse_dense(layer_json, layer_name);
        if (!r)
          return std::unexpected(std::move(r.error()));
        return TilesAndAnims{std::move(*r), {}, {}};
      }
      if (has_sparse)
        return parse_sparse(layer_json, layer_name);
      return std::unexpected(std::format(
          "Tilemap '{}' layer '{}' must have either 'tiles' (dense) or 'objects' (sparse)", id, layer_name));
    };

    std::vector<TilemapLayer> layers;
    layers.reserve(layers_json.size());

    for (const auto &layer_json : layers_json) {
      if (!layer_json.is_object())
        return std::unexpected(std::format("Tilemap '{}' each layer must be an object", id));

      std::string layer_name;
      if (!layer_json.contains("name"))
        return std::unexpected(std::format("Tilemap '{}' a layer is missing 'name'", id));
      try {
        layer_name = layer_json["name"].get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' layer 'name' has wrong type", id));
      }
      if (layer_name.empty())
        return std::unexpected(std::format("Tilemap '{}' layer 'name' must not be empty", id));

      int z_index = 0;
      if (layer_json.contains("z_index")) {
        try {
          z_index = layer_json["z_index"].get<int>();
        } catch (...) {
          return std::unexpected(std::format("Tilemap '{}' layer '{}' field 'z_index' has wrong type", id, layer_name));
        }
        if (z_index < 0)
          z_index = 0;
      }

      auto parse_result = parse_tiles(layer_json, layer_name);
      if (!parse_result)
        return std::unexpected(std::move(parse_result.error()));
      auto [tiles, animated_cells, flip_flags] = std::move(*parse_result);

      // Optional elevation data (same format as tiles: comma-separated rows)
      std::vector<uint8_t> elevation;
      if (layer_json.contains("elevation") && layer_json["elevation"].is_array()) {
        const auto &elev_json = layer_json["elevation"];
        if (static_cast<int>(elev_json.size()) == height) {
          elevation.reserve(static_cast<std::size_t>(expected));
          for (int r = 0; r < height; ++r) {
            std::string row_str;
            try {
              row_str = elev_json[r].get<std::string>();
            } catch (...) {
              elevation.clear();
              break;
            }
            std::istringstream ss(row_str);
            std::string token;
            int col = 0;
            while (std::getline(ss, token, ',') && col < width) {
              try {
                int v = std::stoi(token);
                elevation.push_back(static_cast<uint8_t>(std::clamp(v, 0, 255)));
              } catch (...) {
                elevation.push_back(0); // Default to flat if parse fails
              }
              ++col;
            }
            // Pad to width if row is short
            while (elevation.size() < static_cast<std::size_t>(r + 1) * width) {
              elevation.push_back(0);
            }
          }
        }
      }

      layers.push_back(TilemapLayer{layer_name, z_index, true, std::move(tiles), std::move(animated_cells),
                                    std::move(flip_flags), std::move(elevation)});
    }

    return Tilemap{path.string(),
                   std::move(tilesets),
                   width,
                   height,
                   iso_diamond_w,
                   iso_diamond_h,
                   std::move(layers),
                   std::move(collisions),
                   std::move(collision_triangles)};
  }

} // namespace corundum::gameplay::world::tilemap
