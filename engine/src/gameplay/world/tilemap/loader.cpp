#include <algorithm>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <corundum/resources/sprite_atlas.hpp>
#include <flat_map>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::gameplay::world::tilemap {

  /// A tileset JSON *is* a spritepacker atlas JSON (schema_version 2), plus these optional
  /// tileset-only authoring fields layered on top: material, tile_footprints, animations.
  /// Sprites are referenced by name rather than by local_id/col-row, since MaxRects packing
  /// gives no stable grid position and array order isn't something an author should have to
  /// track across repacks.
  ///
  /// When a sidecar file @c {tileset_path.stem()}.tiledata.json exists alongside the atlas
  /// JSON, its fields override the atlas-sourced authoring data (material, tile_footprints,
  /// pivots, animations).  The sidecar stores pivots in the engine's full-frame/bottom-origin
  /// convention directly, avoiding the need for any conversion round the pivot does NOT go
  /// through.  This sidecar is the canonical home for authoring data — the atlas JSON stays
  /// a pure spritepacker artefact that can be freely regenerated without data loss.
  static std::optional<json> read_sidecar(const fs::path &atlas_path) {
    const fs::path sidecar = atlas_path.parent_path() / (atlas_path.stem().string() + ".tiledata.json");
    std::ifstream f(sidecar);
    if (!f)
      return std::nullopt;
    try {
      auto sc = json::parse(f, nullptr, true, true);
      if (!sc.is_object())
        return std::nullopt;
      return sc;
    } catch (const json::exception &) {
      return std::nullopt;
    }
  }

  std::expected<TilesetInfo, std::string> load_tileset(const fs::path &tileset_path) {
    auto atlas_result = corundum::resources::load_sprite_atlas(tileset_path);
    if (!atlas_result)
      return std::unexpected(atlas_result.error());
    const corundum::resources::SpriteAtlas &atlas = *atlas_result;

    std::ifstream f(tileset_path);
    if (!f)
      return std::unexpected(std::format("Cannot open tileset: {}", tileset_path.string()));
    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed tileset {}: {}", tileset_path.string(), e.what()));
    }

    TilesetInfo info;
    info.source = tileset_path.string();
    info.path = atlas.path;
    info.tile_count = static_cast<int>(atlas.sprites.size());

    info.tile_rects.reserve(atlas.sprites.size());
    info.tile_full_width.reserve(atlas.sprites.size());
    info.tile_full_height.reserve(atlas.sprites.size());
    info.tile_trim_x.reserve(atlas.sprites.size());
    info.tile_trim_y.reserve(atlas.sprites.size());
    info.tile_pivot_x.reserve(atlas.sprites.size());
    info.tile_pivot_y.reserve(atlas.sprites.size());
    info.tile_names.reserve(atlas.sprites.size());

    std::unordered_map<std::string, int> name_to_local_id;
    name_to_local_id.reserve(atlas.sprites.size());

    for (std::size_t i = 0; i < atlas.sprites.size(); ++i) {
      const auto &sprite = atlas.sprites[i];
      info.tile_rects.push_back({sprite.x, sprite.y, sprite.w, sprite.h});
      info.tile_full_width.push_back(sprite.source_width);
      info.tile_full_height.push_back(sprite.source_height);
      info.tile_trim_x.push_back(sprite.trim_x);
      info.tile_trim_y.push_back(sprite.trim_y);

      // Convert spritepacker's pivot (fraction of the *trimmed* box, raster convention: y=0 top,
      // y=1 bottom) into this engine's pivot convention (fraction of the *full* untrimmed frame,
      // y=0 bottom) — see docs/isometric-fundamentals.md §7 for why pivot must stay full-frame
      // relative rather than trimmed-box relative.  These get overridden if a sidecar carries
      // per-sprite pivots (which are already in engine convention).
      const float px_in_trimmed = sprite.pivot_x * static_cast<float>(sprite.w);
      const float py_in_trimmed = sprite.pivot_y * static_cast<float>(sprite.h);
      const float full_px = static_cast<float>(sprite.trim_x) + px_in_trimmed;
      const float full_py = static_cast<float>(sprite.trim_y) + py_in_trimmed;
      const float pivot_x_full = sprite.source_width > 0 ? full_px / static_cast<float>(sprite.source_width) : 0.5f;
      const float pivot_y_full_raster =
          sprite.source_height > 0 ? full_py / static_cast<float>(sprite.source_height) : 1.f;
      info.tile_pivot_x.push_back(pivot_x_full);
      info.tile_pivot_y.push_back(1.f - pivot_y_full_raster);

      info.tile_names.push_back(sprite.name);
      name_to_local_id.emplace(sprite.name, static_cast<int>(i));
    }

    // ── Sidecar override (optional) ──────────────────────────────────────
    // The sidecar may carry tile_footprints, pivots, animations, and material —
    // all in engine convention.  When present these override the atlas-sourced
    // defaults.  The atlas JSON becomes a pure spritepacker artefact; authoring
    // data lives safely adjacent to it.
    const std::optional<json> sidecar = read_sidecar(tileset_path);

    auto resolve_name = [&name_to_local_id](const std::string &name) -> std::optional<int> {
      auto it = name_to_local_id.find(name);
      if (it == name_to_local_id.end())
        return std::nullopt;
      return it->second;
    };

    // ── material ─────────────────────────────────────────────────────────

    const json *material_src = &j;
    if (sidecar && sidecar->contains("material") && (*sidecar)["material"].is_string())
      material_src = &*sidecar;

    if (material_src->contains("material")) {
      try {
        info.material = (*material_src)["material"].get<std::string>();
      } catch (const nlohmann::json::exception &) {
      }
    }

    // ── tile_footprints ──────────────────────────────────────────────────

    {
      const json *fp_src = &j;
      if (sidecar && sidecar->contains("tile_footprints") && (*sidecar)["tile_footprints"].is_array())
        fp_src = &*sidecar;

      if (fp_src->contains("tile_footprints") && (*fp_src)["tile_footprints"].is_array()) {
        const auto &fps_json = (*fp_src)["tile_footprints"];
        for (std::size_t fi = 0; fi < fps_json.size(); ++fi) {
          const auto &entry = fps_json[fi];
          if (!entry.is_object())
            continue;
          std::string name;
          int w = 1, h = 1;
          try {
            name = entry.at("name").get<std::string>();
          } catch (...) {
            return std::unexpected(
                std::format("Tileset '{}' tile_footprints[{}] missing 'name'", tileset_path.string(), fi));
          }
          const auto local_id = resolve_name(name);
          if (!local_id)
            return std::unexpected(std::format("Tileset '{}' tile_footprints[{}] unknown sprite name '{}'",
                                               tileset_path.string(), fi, name));
          if (entry.contains("w")) {
            try {
              w = entry["w"].get<int>();
            } catch (const nlohmann::json::exception &) {
            }
          }
          if (entry.contains("h")) {
            try {
              h = entry["h"].get<int>();
            } catch (const nlohmann::json::exception &) {
            }
          }
          info.tile_footprints[*local_id] = {std::max(1, w), std::max(1, h)};
        }
      }
    }

    // ── pivots (sidecar only — engine convention, no conversion) ────────

    if (sidecar && sidecar->contains("pivots") && (*sidecar)["pivots"].is_array()) {
      for (const auto &pe : (*sidecar)["pivots"]) {
        if (!pe.is_object())
          continue;
        std::string name;
        try {
          name = pe.at("name").get<std::string>();
        } catch (...) {
          continue;
        }
        const auto local_id = resolve_name(name);
        if (!local_id)
          continue;
        const auto idx = static_cast<std::size_t>(*local_id);
        if (pe.contains("pivot_x")) {
          try {
            info.tile_pivot_x[idx] = pe["pivot_x"].get<float>();
          } catch (const nlohmann::json::exception &) {
          }
        }
        if (pe.contains("pivot_y")) {
          try {
            info.tile_pivot_y[idx] = pe["pivot_y"].get<float>();
          } catch (const nlohmann::json::exception &) {
          }
        }
      }
    }

    // ── animations ───────────────────────────────────────────────────────

    {
      const json *anim_src = &j;
      if (sidecar && sidecar->contains("animations") && (*sidecar)["animations"].is_object())
        anim_src = &*sidecar;

      if (anim_src->contains("animations")) {
        const auto &anim_obj = (*anim_src)["animations"];
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
            std::string frame_name;
            try {
              frame_name = frame.get<std::string>();
            } catch (...) {
              return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] must be a sprite name",
                                                 tileset_path.string(), ci, fi));
            }
            const auto local_id = resolve_name(frame_name);
            if (!local_id)
              return std::unexpected(
                  std::format("Tileset '{}' animations.clips[{}] frames[{}] unknown sprite name '{}'",
                              tileset_path.string(), ci, fi, frame_name));
            anim.frames.push_back(*local_id);
          }
          info.animations[name] = std::move(anim);
        }
      }
    }

    return info;
  }

  /// Migrates a tilemap JSON object in place from @p from_version up to k_tilemap_schema_version,
  /// applying each version-to-version step in sequence. No migrations exist yet — schema_version 1
  /// is both the legacy (absent-field) format and the current format, so this is a no-op today. This
  /// is the hook point for future schema changes, e.g.:
  ///   if (from_version < 2) { /* rewrite v1 fields into v2 shape */ from_version = 2; }
  /// Existing steps must never be edited once shipped, since already-migrated files may depend on
  /// the exact transformation a step performed.
  static std::expected<void, std::string> migrate_tilemap_json(json & /*j*/, int from_version, const fs::path &path) {
    if (from_version < 1)
      return std::unexpected(std::format("Tilemap '{}' has invalid schema_version {}", path.string(), from_version));
    return {};
  }

  /// Parse the optional "schema_version" field. Absent -> legacy version 1.
  static std::expected<int, std::string> parse_schema_version(const json &j, const fs::path &path) {
    if (!j.contains("schema_version"))
      return 1;
    if (!j["schema_version"].is_number_integer())
      return std::unexpected(std::format("Tilemap '{}' field 'schema_version' must be an integer", path.string()));
    return j["schema_version"].get<int>();
  }

  std::expected<Tilemap, std::string> load_tilemap(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open tilemap: {}", path.string()));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed tilemap {}: {}", path.string(), e.what()));
    }

    const auto sv = parse_schema_version(j, path);
    if (!sv)
      return std::unexpected(sv.error());
    const int schema_version = *sv;
    if (schema_version > k_tilemap_schema_version)
      return std::unexpected(std::format(
          "Tilemap '{}' has schema_version {}, newer than this engine supports (max {}) — update the engine",
          path.string(), schema_version, k_tilemap_schema_version));
    if (schema_version < k_tilemap_schema_version) {
      auto mig = migrate_tilemap_json(j, schema_version, path);
      if (!mig)
        return std::unexpected(mig.error());
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
      collisions.cols.reserve(col_json.size());
      collisions.rows.reserve(col_json.size());
      collisions.col_spans.reserve(col_json.size());
      collisions.row_spans.reserve(col_json.size());
      collisions.elevations.reserve(col_json.size());
      for (std::size_t ci = 0; ci < col_json.size(); ++ci) {
        const auto &entry = col_json[ci];
        if (!entry.is_object())
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] must be an object", id, ci));
        CollisionRect r;
        try {
          r.col = entry.at("x").get<float>();
          r.row = entry.at("y").get<float>();
          r.col_span = entry.at("w").get<float>();
          r.row_span = entry.at("h").get<float>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' collisions[{}] missing or invalid field (x, y, w, h required)", id, ci));
        }
        if (r.col_span <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] 'w' must be positive", id, ci));
        if (r.row_span <= 0.f)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] 'h' must be positive", id, ci));
        const uint8_t elevation = entry.contains("elevation") ? entry["elevation"].get<uint8_t>() : uint8_t{0};
        collisions.push_back(r.col, r.row, r.col_span, r.row_span, elevation);
      }
    }

    // collision_triangles (optional)
    CollisionTriangles collision_triangles;
    if (j.contains("collision_triangles")) {
      if (!j["collision_triangles"].is_array())
        return std::unexpected(std::format("Tilemap '{}' field 'collision_triangles' must be an array", id));
      const auto &tri_json = j["collision_triangles"];
      collision_triangles.cols.reserve(tri_json.size());
      collision_triangles.rows.reserve(tri_json.size());
      collision_triangles.col_spans.reserve(tri_json.size());
      collision_triangles.row_spans.reserve(tri_json.size());
      collision_triangles.cuts.reserve(tri_json.size());
      collision_triangles.elevations.reserve(tri_json.size());
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
        const uint8_t elevation = entry.contains("elevation") ? entry["elevation"].get<uint8_t>() : uint8_t{0};
        collision_triangles.push_back(x, y, w, h, cut, elevation);
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
      ts.tile_count = ts.info.tile_count;
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
    auto require_pos_int = [&j, &id](const char *key) -> std::expected<int, std::string> {
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
    auto parse_dense = [&height, &id, &expected, &width,
                        &tilesets](const json &layer_json,
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

    auto parse_sparse = [&expected, &width, &height, &id,
                         &tilesets](const json &layer_json,
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

    auto parse_tiles = [&id, &parse_dense,
                        &parse_sparse](const json &layer_json,
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

      // Optional per-cell material overrides: [{"col":x,"row":y,"material":"tag"}], sparse.
      std::flat_map<int, std::string> material_overrides;
      if (layer_json.contains("material_overrides") && layer_json["material_overrides"].is_array()) {
        const auto &mat_json = layer_json["material_overrides"];
        for (std::size_t mi = 0; mi < mat_json.size(); ++mi) {
          const auto &entry = mat_json[mi];
          if (!entry.is_object())
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' material_overrides[{}] must be an object", id, layer_name, mi));
          int mcol, mrow;
          std::string material;
          try {
            mcol = entry.at("col").get<int>();
            mrow = entry.at("row").get<int>();
            material = entry.at("material").get<std::string>();
          } catch (...) {
            return std::unexpected(std::format(
                "Tilemap '{}' layer '{}' material_overrides[{}] missing or invalid 'col', 'row', or 'material'", id,
                layer_name, mi));
          }
          if (mcol < 0 || mcol >= width || mrow < 0 || mrow >= height)
            return std::unexpected(std::format("Tilemap '{}' layer '{}' material_overrides[{}] position ({}, {}) "
                                               "out of bounds",
                                               id, layer_name, mi, mcol, mrow));
          const std::size_t flat_idx =
              static_cast<std::size_t>(mrow) * static_cast<std::size_t>(width) + static_cast<std::size_t>(mcol);
          if (flat_idx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::unexpected(std::format("Tilemap '{}' layer '{}' material_overrides[{}] index {} "
                                               "exceeds int maximum",
                                               id, layer_name, mi, flat_idx));
          material_overrides[static_cast<int>(flat_idx)] = std::move(material);
        }
      }

      // Optional per-cell ramps: [{"col":x,"row":y,"axis":"ns"|"ew"}], sparse.
      std::flat_map<int, RampAxis> ramps;
      if (layer_json.contains("ramps") && layer_json["ramps"].is_array()) {
        const auto &ramps_json = layer_json["ramps"];
        for (std::size_t ri = 0; ri < ramps_json.size(); ++ri) {
          const auto &entry = ramps_json[ri];
          if (!entry.is_object())
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' ramps[{}] must be an object", id, layer_name, ri));
          int rcol;
          int rrow;
          std::string axis_str;
          try {
            rcol = entry.at("col").get<int>();
            rrow = entry.at("row").get<int>();
            axis_str = entry.at("axis").get<std::string>();
          } catch (const nlohmann::json::exception &) {
            return std::unexpected(std::format(
                "Tilemap '{}' layer '{}' ramps[{}] missing or invalid 'col', 'row', or 'axis'", id, layer_name, ri));
          }
          if (rcol < 0 || rcol >= width || rrow < 0 || rrow >= height)
            return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] position ({}, {}) out of bounds", id,
                                               layer_name, ri, rcol, rrow));
          RampAxis axis;
          if (axis_str == "ns")
            axis = RampAxis::NORTH_SOUTH;
          else if (axis_str == "ew")
            axis = RampAxis::EAST_WEST;
          else
            return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] axis '{}' must be \"ns\" or \"ew\"",
                                               id, layer_name, ri, axis_str));
          const std::size_t flat_idx =
              static_cast<std::size_t>(rrow) * static_cast<std::size_t>(width) + static_cast<std::size_t>(rcol);
          if (flat_idx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] index {} exceeds int maximum", id,
                                               layer_name, ri, flat_idx));
          ramps[static_cast<int>(flat_idx)] = axis;
        }
      }

      layers.push_back(TilemapLayer{
          .name = layer_name,
          .z_index = z_index,
          .visible = true,
          .tiles = std::move(tiles),
          .animated_cells = std::move(animated_cells),
          .flip_flags = std::move(flip_flags),
          .elevation = std::move(elevation),
          .material_overrides = std::move(material_overrides),
          .ramps = std::move(ramps),
      });
      layers.back().bake_render_cache(width, height);
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
