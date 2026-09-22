// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "encoding.hpp"

#include <algorithm>
#include <corundum/core/schema_version.hpp>
#include <corundum/sprites/sprite_atlas.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace corundum::world::tilemap {

  namespace {

    // Tilesets are shared across many chunks/maps; parsing the same atlas from disk once per
    // chunk was the dominant chunk-streaming cost (~43ms per 16x16 chunk, recomputing the same
    // TilesetInfo for the handful of shared tilesets every time). Cache the fully-resolved result
    // keyed by source path. Main-thread only (render/load), so no locking needed.
    std::unordered_map<std::string, TilesetInfo> &tileset_cache() {
      static std::unordered_map<std::string, TilesetInfo> cache;
      return cache;
    }

    /// Read the optional sidecar @c {atlas_path.stem()}.tiledata.json sitting next to @p atlas_path.
    /// @return The parsed object, or std::nullopt when the sidecar is absent or malformed — a broken
    ///         sidecar is treated as absent rather than failing the whole load.
    std::optional<json> read_sidecar(const fs::path &atlas_path) {
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

    /// Parse @p path as JSON.
    /// @param label Asset kind for error messages (e.g. "tileset", "tilemap").
    /// @return The parsed document, or a "Cannot open" / "Malformed" message naming @p label.
    std::expected<json, std::string> parse_json_file(const fs::path &path, std::string_view label) {
      std::ifstream file(path);
      if (!file)
        return std::unexpected(std::format("Cannot open {}: {}", label, path.string()));
      try {
        return json::parse(file, nullptr, true, true);
      } catch (const json::exception &e) {
        return std::unexpected(std::format("Malformed {} {}: {}", label, path.string(), e.what()));
      }
    }

    /// Per-tile data resolved from a spritepacker atlas, plus the sprite-name → local-id lookup the
    /// animations block uses to resolve frame names.
    struct TilesetTiles {
      TilesetInfo info;
      std::unordered_map<std::string, int> name_to_local_id;
    };

    /// Build @p atlas's per-tile arrays (rects, trim, pivot, names) into a fresh TilesetInfo.
    TilesetTiles build_tileset_tiles(const corundum::sprites::SpriteAtlas &atlas, const fs::path &tileset_path) {
      TilesetTiles tiles;
      TilesetInfo &info = tiles.info;
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

      tiles.name_to_local_id.reserve(atlas.sprites.size());

      for (std::size_t i = 0; i < atlas.sprites.size(); ++i) {
        const auto &sprite = atlas.sprites[i];
        info.tile_rects.push_back({.x = sprite.x, .y = sprite.y, .width = sprite.w, .height = sprite.h});
        info.tile_full_width.push_back(sprite.source_width);
        info.tile_full_height.push_back(sprite.source_height);
        info.tile_trim_x.push_back(sprite.trim_x);
        info.tile_trim_y.push_back(sprite.trim_y);

        // Pivots stay full-frame relative so untrimmed source padding (where tilemap alignment
        // lives) survives repacking; resolve_pivot collapses spritepacker's pivot basis into the
        // engine's full-frame, bottom-origin convention.
        const corundum::sprites::PivotPoint pivot = corundum::sprites::resolve_pivot(sprite, atlas.pivot_basis);
        info.tile_pivot_x.push_back(pivot.x);
        info.tile_pivot_y.push_back(pivot.y);

        info.tile_names.push_back(sprite.name);
        tiles.name_to_local_id.emplace(sprite.name, static_cast<int>(i));
      }
      return tiles;
    }

    /// Resolve a tileset's default material tag, preferring the sidecar's `material` string when
    /// present; empty when neither the sidecar nor the atlas JSON sets one.
    std::string resolve_tileset_material(const json &atlas_json, const json *sidecar) {
      const json *source = &atlas_json;
      if (sidecar != nullptr && sidecar->contains("material") && (*sidecar)["material"].is_string())
        source = sidecar;
      if (const auto it = source->find("material"); it != source->end() && it->is_string())
        return it->get<std::string>();
      return {};
    }

    /// @return @p name's local tile id in @p name_to_local_id, or nullopt if the atlas has no such sprite.
    std::optional<int> resolve_sprite_id(const std::unordered_map<std::string, int> &name_to_local_id,
                                         const std::string &name) {
      const auto it = name_to_local_id.find(name);
      if (it == name_to_local_id.end())
        return std::nullopt;
      return it->second;
    }

    /// A parsed `animations.clips` entry: its name paired with the resolved clip.
    using AnimationClip = std::pair<std::string, TileAnimation>;

    /// Parse one `animations.clips` entry.
    /// @param clip_index Index of @p clip in the clips array, for error messages.
    /// @param default_fps Clip fps used when the entry does not set its own.
    /// @return The (name, clip) pair, or a message describing the first malformed field.
    std::expected<AnimationClip, std::string>
    parse_animation_clip(const json &clip, std::size_t clip_index, float default_fps, const fs::path &tileset_path,
                         const std::unordered_map<std::string, int> &name_to_local_id) {
      if (!clip.is_object())
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] must be an object", tileset_path.string(), clip_index));

      std::string name;
      try {
        name = clip.at("name").get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tileset '{}' animations.clips[{}] missing or invalid 'name'",
                                           tileset_path.string(), clip_index));
      }
      if (name.empty())
        return std::unexpected(std::format("Tileset '{}' animations.clips[{}] 'name' must not be empty",
                                           tileset_path.string(), clip_index));

      if (!clip.contains("frames") || !clip["frames"].is_array())
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] missing 'frames' array", tileset_path.string(), clip_index));

      TileAnimation anim;
      anim.fps = default_fps;
      if (clip.contains("fps")) {
        try {
          anim.fps = clip["fps"].get<float>();
        } catch (...) {
          return std::unexpected(
              std::format("Tileset '{}' animations.clips[{}] 'fps' has wrong type", tileset_path.string(), clip_index));
        }
        if (anim.fps <= 0.f)
          return std::unexpected(std::format("Tileset '{}' animations.clips[{}] 'fps' must be positive",
                                             tileset_path.string(), clip_index));
      }

      for (std::size_t fi = 0; fi < clip["frames"].size(); ++fi) {
        const auto &frame = clip["frames"][fi];
        std::string frame_name;
        try {
          frame_name = frame.get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] must be a sprite name",
                                             tileset_path.string(), clip_index, fi));
        }
        const auto local_id = resolve_sprite_id(name_to_local_id, frame_name);
        if (!local_id)
          return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] unknown sprite name '{}'",
                                             tileset_path.string(), clip_index, fi, frame_name));
        anim.frames.push_back(*local_id);
      }
      if (anim.frames.empty())
        return std::unexpected(std::format("Tileset '{}' animations.clips[{}] 'frames' must not be empty",
                                           tileset_path.string(), clip_index));

      return AnimationClip{std::move(name), std::move(anim)};
    }

    /// Parse a tileset's `animations` block — the sidecar's when present, else the atlas JSON's.
    /// @return name → clip, or a message describing the first malformed clip; empty when absent.
    std::expected<std::flat_map<std::string, TileAnimation>, std::string>
    parse_tileset_animations(const json &atlas_json, const json *sidecar, const fs::path &tileset_path,
                             const std::unordered_map<std::string, int> &name_to_local_id) {
      const json *source = &atlas_json;
      if (sidecar != nullptr && sidecar->contains("animations") && (*sidecar)["animations"].is_object())
        source = sidecar;

      std::flat_map<std::string, TileAnimation> animations;
      if (!source->contains("animations"))
        return animations;

      const auto &anim_obj = (*source)["animations"];
      if (!anim_obj.is_object())
        return std::unexpected(std::format("Tileset '{}' field 'animations' must be an object", tileset_path.string()));

      float default_fps = 5.f;
      if (anim_obj.contains("fps")) {
        try {
          default_fps = anim_obj["fps"].get<float>();
        } catch (...) {
          return std::unexpected(std::format("Tileset '{}' animations 'fps' has wrong type", tileset_path.string()));
        }
        if (default_fps <= 0.f)
          return std::unexpected(std::format("Tileset '{}' animations 'fps' must be positive", tileset_path.string()));
      }

      if (!anim_obj.contains("clips") || !anim_obj["clips"].is_array())
        return std::unexpected(std::format("Tileset '{}' animations missing 'clips' array", tileset_path.string()));

      const auto &clips = anim_obj["clips"];
      for (std::size_t ci = 0; ci < clips.size(); ++ci) {
        auto clip = parse_animation_clip(clips[ci], ci, default_fps, tileset_path, name_to_local_id);
        if (!clip)
          return std::unexpected(std::move(clip.error()));
        animations[clip->first] = std::move(clip->second);
      }
      return animations;
    }

  } // namespace

  void clear_tileset_cache() {
    tileset_cache().clear();
  }

  /// A tileset JSON *is* a spritepacker atlas JSON (schema_version 2), plus these optional
  /// tileset-only authoring fields layered on top: material, animations.
  /// Sprites are referenced by name rather than by local_id/col-row, since MaxRects packing
  /// gives no stable grid position and array order isn't something an author should have to
  /// track across repacks.
  ///
  /// When a sidecar file @c {tileset_path.stem()}.tiledata.json exists alongside the atlas JSON,
  /// its fields override the atlas-sourced authoring data (material, animations). Pivots and tile
  /// footprints are intentionally NOT read from the sidecar — pivots live in the atlas JSON
  /// (spritepacker `--pivot full-canvas`), so re-packing never loses alignment.
  std::expected<TilesetInfo, std::string> load_tileset(const fs::path &tileset_path) {
    auto &cache = tileset_cache();
    const std::string key = tileset_path.string();
    if (const auto it = cache.find(key); it != cache.end())
      return it->second;

    auto atlas = corundum::sprites::load_sprite_atlas(tileset_path);
    if (!atlas)
      return std::unexpected(atlas.error());

    auto atlas_json = parse_json_file(tileset_path, "tileset");
    if (!atlas_json)
      return std::unexpected(std::move(atlas_json.error()));

    const std::optional<json> sidecar = read_sidecar(tileset_path);
    const json *sidecar_json = sidecar ? &*sidecar : nullptr;

    TilesetTiles tiles = build_tileset_tiles(*atlas, tileset_path);
    tiles.info.material = resolve_tileset_material(*atlas_json, sidecar_json);

    auto animations = parse_tileset_animations(*atlas_json, sidecar_json, tileset_path, tiles.name_to_local_id);
    if (!animations)
      return std::unexpected(std::move(animations.error()));
    tiles.info.animations = std::move(*animations);

    cache.emplace(key, tiles.info);
    return tiles.info;
  }

  /// Migrates a tilemap JSON object in place from @p from_version up to k_tilemap_schema_version,
  /// applying each version-to-version step in sequence and returning the version reached. No
  /// migrations exist yet — schema_version 1 is both the legacy (absent-field) format and the
  /// current format — so this returns @p from_version unchanged. When k_tilemap_schema_version is
  /// raised, append a step and advance @p from_version with it, e.g.:
  ///   if (from_version < 2) { /* rewrite v1 fields into v2 shape */ from_version = 2; }
  /// The caller rejects a result below the current version, so leaving this stub unchanged after a
  /// bump fails loudly rather than mis-parsing an old file. Existing steps must never be edited
  /// once shipped, since already-migrated files may depend on the exact transformation a step
  /// performed.
  namespace {

    std::expected<int, std::string> migrate_tilemap_json(json & /*j*/, int from_version, const std::string & /*path*/) {
      return from_version;
    }

    /// Split @p row_str into comma-separated integers, rejecting a count other than @p width.
    /// @param what Human-readable locator (e.g. "layer 'ground' row 3") prefixed to any error.
    /// @return Exactly @p width values, or a message describing the first malformed token.
    std::expected<std::vector<int>, std::string> parse_csv_row(const std::string &row_str, int width,
                                                               const std::string &what) {
      std::vector<int> values;
      values.reserve(static_cast<std::size_t>(width));
      std::istringstream stream(row_str);
      std::string token;
      while (std::getline(stream, token, ',')) {
        if (std::cmp_greater_equal(values.size(), width))
          return std::unexpected(std::format("{} has more than {} values", what, width));
        try {
          values.push_back(std::stoi(token));
        } catch (...) {
          return std::unexpected(std::format("{} value {}: not an integer", what, values.size()));
        }
      }
      if (std::cmp_not_equal(values.size(), width))
        return std::unexpected(std::format("{} has {} values, expected {}", what, values.size(), width));
      return values;
    }

    /// Parse a collision entry's optional "elevation" field, range-checked to [0, 255]; absent -> 0.
    std::expected<uint8_t, std::string> parse_collision_elevation(const json &entry) {
      if (!entry.contains("elevation"))
        return uint8_t{0};
      int value = 0;
      try {
        value = entry["elevation"].get<int>();
      } catch (...) {
        return std::unexpected(std::string{"'elevation' has wrong type (expected integer)"});
      }
      if (value < 0 || value > 255)
        return std::unexpected(std::format("'elevation' value {} out of range [0, 255]", value));
      return static_cast<uint8_t>(value);
    }

  } // namespace

  // NOLINTNEXTLINE(readability-function-cognitive-complexity): large flat format validator.
  std::expected<Tilemap, std::string> load_tilemap(const fs::path &path) {
    auto parsed = parse_json_file(path, "tilemap");
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    json &j = *parsed;

    auto prepared =
        core::prepare_schema_version(j, k_tilemap_schema_version, "Tilemap", path.string(), migrate_tilemap_json);
    if (!prepared)
      return std::unexpected(prepared.error());

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
        auto elevation = parse_collision_elevation(entry);
        if (!elevation)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] {}", id, ci, elevation.error()));
        collisions.push_back(r.col, r.row, r.col_span, r.row_span, *elevation);
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
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
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
        const std::optional<TriangleCut> cut = triangle_cut_from_string(cut_str);
        if (!cut.has_value())
          return std::unexpected(
              std::format("Tilemap '{}' collision_triangles[{}] 'cut' must be NW, NE, SW, or SE", id, ci));
        auto elevation = parse_collision_elevation(entry);
        if (!elevation)
          return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] {}", id, ci, elevation.error()));
        collision_triangles.push_back(x, y, w, h, *cut, *elevation);
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
      int first_gid = 0;
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
      auto tileset_result = load_tileset(fs::path(source));
      if (!tileset_result.has_value())
        return std::unexpected(std::format("[{}] tilesets[{}]: {}", id, ti, tileset_result.error()));
      TilemapTileset ts;
      ts.info = std::move(*tileset_result);
      ts.first_gid = static_cast<TileId>(first_gid);
      if (static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.info.tile_count) >
          static_cast<uint32_t>(k_empty_tile))
        return std::unexpected(
            std::format("Tilemap '{}' tilesets[{}] GID range [{}, {}) exceeds reserved sentinel {}", id, ti,
                        ts.first_gid, static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.info.tile_count),
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
      const TileId expected_next = tilesets[i].first_gid + static_cast<TileId>(tilesets[i].info.tile_count);
      if (tilesets[i].first_gid == tilesets[i + 1].first_gid)
        return std::unexpected(
            std::format("Tilemap '{}' tilesets have duplicate first_gid={}", id, tilesets[i].first_gid));
      if (tilesets[i + 1].first_gid != expected_next)
        return std::unexpected(
            std::format("Tilemap '{}' tilesets are not contiguous: gap or overlap between first_gid={} "
                        "(tile_count={}) and first_gid={})",
                        id, tilesets[i].first_gid, tilesets[i].info.tile_count, tilesets[i + 1].first_gid));
    }

    // width, height
    const auto require_positive_int = [&j, &id](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Tilemap '{}' missing '{}'", id, key));
      int v = 0;
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
      auto r = require_positive_int("width");
      if (!r)
        return std::unexpected(std::move(r.error()));
      width = *r;
    }
    int height = 0;
    {
      auto r = require_positive_int("height");
      if (!r)
        return std::unexpected(std::move(r.error()));
      height = *r;
    }

    int iso_diamond_w = 0;
    int iso_diamond_h = 0;
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

    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // flat_map keys and the animation/flip indices are ints, so a cell count above INT_MAX would
    // silently wrap those indices; reject it rather than overflow them.
    if (expected > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      return std::unexpected(
          std::format("Tilemap '{}' dimensions {}x{} exceed the supported cell count", id, width, height));

    // Dense format: array of height strings, each a comma-separated row of width tile IDs.
    auto parse_dense = [&height, &id, &expected, &width,
                        &tilesets](const json &layer_json,
                                   const std::string &layer_name) -> std::expected<std::vector<TileId>, std::string> {
      const auto &tiles_json = layer_json["tiles"];
      if (std::cmp_not_equal(tiles_json.size(), height))
        return std::unexpected(std::format("Tilemap '{}' layer '{}' tiles row count mismatch: expected {}, got {}", id,
                                           layer_name, height, tiles_json.size()));

      std::vector<TileId> tiles;
      tiles.reserve(expected);

      for (int r = 0; r < height; ++r) {
        std::string row_str;
        try {
          row_str = tiles_json[r].get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tilemap '{}' layer '{}' tiles[{}] must be a string", id, layer_name, r));
        }

        auto row = parse_csv_row(row_str, width, std::format("Tilemap '{}' layer '{}' row {}", id, layer_name, r));
        if (!row)
          return std::unexpected(std::move(row.error()));
        for (int col = 0; col < width; ++col) {
          const int v = (*row)[col];
          if (v < 0)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} col {}: id={} is negative", id, layer_name, r, col, v));
          if (std::cmp_not_equal(v, k_empty_tile) && find_tileset(tilesets, static_cast<TileId>(v)) == nullptr)
            return std::unexpected(std::format(
                "Tilemap '{}' layer '{}' row {} col {}: GID={} not covered by any tileset", id, layer_name, r, col, v));
          tiles.push_back(static_cast<TileId>(v));
        }
      }
      return tiles;
    };

    using TilesAndAnimations =
        std::tuple<std::vector<TileId>, std::flat_map<int, AnimatedCell>, std::flat_map<int, uint8_t>>;

    // NOLINTNEXTLINE(readability-function-cognitive-complexity): single sparse-layer validator.
    auto parse_sparse = [&expected, &width, &height, &id,
                         &tilesets](const json &layer_json,
                                    const std::string &layer_name) -> std::expected<TilesAndAnimations, std::string> {
      std::vector<TileId> tiles(expected, k_empty_tile);
      std::vector<bool> occupied(expected, false);
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

        int col = 0;
        int row = 0;
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

        const auto idx =
            (static_cast<std::size_t>(row) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(col);
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
          const std::optional<uint8_t> parsed_flip = flip_flags_from_string(flip_str);
          if (!parsed_flip.has_value())
            return std::unexpected(std::format(
                R"(Tilemap '{}' layer '{}' objects[{}] 'flip' must be "H", "V", or "HV")", id, layer_name, i));
          flip = *parsed_flip;
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
            const auto it = ts.info.animations.find(anim_name);
            if (it == ts.info.animations.end())
              continue;
            const auto &ta = it->second;
            cell.anim_name = anim_name;
            cell.fps = ta.fps;
            cell.frame_gids.reserve(ta.frames.size());
            for (const int local_id : ta.frames)
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
          int tile_id = 0;
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

    const auto parse_tiles = [&id, &parse_dense, &parse_sparse](
                                 const json &layer_json,
                                 const std::string &layer_name) -> std::expected<TilesAndAnimations, std::string> {
      const bool has_dense = layer_json.contains("tiles") && layer_json["tiles"].is_array();
      const bool has_sparse = layer_json.contains("objects") && layer_json["objects"].is_array();
      if (has_dense && has_sparse)
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' must have 'tiles' or 'objects', not both", id, layer_name));
      if (has_dense) {
        auto r = parse_dense(layer_json, layer_name);
        if (!r)
          return std::unexpected(std::move(r.error()));
        return TilesAndAnimations{std::move(*r), std::flat_map<int, AnimatedCell>{}, std::flat_map<int, uint8_t>{}};
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
        z_index = std::max(z_index, 0);
      }

      bool depth_sorted = false;
      if (layer_json.contains("depth_sorted")) {
        try {
          depth_sorted = layer_json["depth_sorted"].get<bool>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' field 'depth_sorted' has wrong type", id, layer_name));
        }
      }

      auto parse_result = parse_tiles(layer_json, layer_name);
      if (!parse_result)
        return std::unexpected(std::move(parse_result.error()));
      auto [tiles, animated_cells, flip_flags] = std::move(*parse_result);

      // Optional elevation data (same rows-of-CSV layout as 'tiles'); absent means all flat.
      std::vector<uint8_t> elevation;
      if (layer_json.contains("elevation")) {
        const auto &elev_json = layer_json["elevation"];
        if (!elev_json.is_array())
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' field 'elevation' must be an array", id, layer_name));
        if (std::cmp_not_equal(elev_json.size(), height))
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' elevation row count mismatch: expected {}, got {}", id, layer_name,
                          height, elev_json.size()));
        elevation.reserve(expected);
        for (int r = 0; r < height; ++r) {
          std::string row_str;
          try {
            row_str = elev_json[r].get<std::string>();
          } catch (...) {
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' elevation[{}] must be a string", id, layer_name, r));
          }
          auto row =
              parse_csv_row(row_str, width, std::format("Tilemap '{}' layer '{}' elevation row {}", id, layer_name, r));
          if (!row)
            return std::unexpected(std::move(row.error()));
          for (int col = 0; col < width; ++col) {
            const int v = (*row)[col];
            if (v < 0 || v > 255)
              return std::unexpected(
                  std::format("Tilemap '{}' layer '{}' elevation row {} col {}: value {} out of range [0, 255]", id,
                              layer_name, r, col, v));
            elevation.push_back(static_cast<uint8_t>(v));
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
          int mcol = 0;
          int mrow = 0;
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
              (static_cast<std::size_t>(mrow) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(mcol);
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
          int rcol = 0;
          int rrow = 0;
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
          const std::optional<RampAxis> axis = ramp_axis_from_string(axis_str);
          if (!axis.has_value())
            return std::unexpected(std::format(R"(Tilemap '{}' layer '{}' ramps[{}] axis '{}' must be "ns" or "ew")",
                                               id, layer_name, ri, axis_str));
          const std::size_t flat_idx =
              (static_cast<std::size_t>(rrow) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(rcol);
          if (flat_idx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] index {} exceeds int maximum", id,
                                               layer_name, ri, flat_idx));
          ramps[static_cast<int>(flat_idx)] = *axis;
        }
      }

      layers.push_back(TilemapLayer{
          .name = layer_name,
          .z_index = z_index,
          .depth_sorted = depth_sorted,
          .visible = true,
          .tiles = std::move(tiles),
          .animated_cells = std::move(animated_cells),
          .flip_flags = std::move(flip_flags),
          .elevation = std::move(elevation),
          .material_overrides = std::move(material_overrides),
          .ramps = std::move(ramps),
          .baked_flip_flags = {},
          .baked_animation_index = {},
          .baked_animations = {},
      });
      layers.back().bake_render_cache(width, height);
    }

    int max_tile_full_w = 0;
    int max_tile_full_h = 0;
    for (const auto &ts : tilesets) {
      if (!ts.info.tile_full_width.empty()) {
        max_tile_full_w =
            std::max(max_tile_full_w, *std::ranges::max_element(ts.info.tile_full_width.begin(),
                                                                ts.info.tile_full_width.end(), std::less{}));
      }
      if (!ts.info.tile_full_height.empty()) {
        max_tile_full_h =
            std::max(max_tile_full_h, *std::ranges::max_element(ts.info.tile_full_height.begin(),
                                                                ts.info.tile_full_height.end(), std::less{}));
      }
    }

    return Tilemap{
        .path = path.string(),
        .tilesets = std::move(tilesets),
        .width = width,
        .height = height,
        .iso_diamond_w = iso_diamond_w,
        .iso_diamond_h = iso_diamond_h,
        .layers = std::move(layers),
        .collisions = std::move(collisions),
        .collision_triangles = std::move(collision_triangles),
        .max_tile_full_w = max_tile_full_w,
        .max_tile_full_h = max_tile_full_h,
    };
  }

} // namespace corundum::world::tilemap
