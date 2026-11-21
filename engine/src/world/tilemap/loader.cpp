// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "encoding.hpp"

#include <algorithm>
#include <corundum/core/content_source.hpp>
#include <corundum/core/json_io.hpp>
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
#include <limits>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
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
    std::optional<json> read_sidecar(const core::ContentSource &content, const fs::path &atlas_path) {
      const fs::path sidecar = atlas_path.parent_path() / (atlas_path.stem().string() + ".tiledata.json");
      auto sc = corundum::core::read_json(content, sidecar);
      if (!sc || !sc->is_object())
        return std::nullopt;
      return *sc;
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
    struct AnimationClip {
      std::string name;
      TileAnimation animation;
    };

    /// Read a clip's required non-empty `name`.
    std::expected<std::string, std::string> parse_clip_name(const json &clip, std::size_t clip_index,
                                                            const fs::path &tileset_path) {
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
      return name;
    }

    /// Read a clip's optional positive `fps`, falling back to @p default_fps.
    std::expected<float, std::string> parse_clip_fps(const json &clip, std::size_t clip_index, float default_fps,
                                                     const fs::path &tileset_path) {
      if (!clip.contains("fps"))
        return default_fps;
      float fps = 0.f;
      try {
        fps = clip["fps"].get<float>();
      } catch (...) {
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] 'fps' has wrong type", tileset_path.string(), clip_index));
      }
      if (fps <= 0.f)
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] 'fps' must be positive", tileset_path.string(), clip_index));
      return fps;
    }

    /// Resolve a clip's `frames` array of sprite names to tileset-local ids.
    std::expected<std::vector<int>, std::string>
    parse_clip_frames(const json &clip, std::size_t clip_index, const fs::path &tileset_path,
                      const std::unordered_map<std::string, int> &name_to_local_id) {
      const auto &frames_json = clip["frames"];
      std::vector<int> frames;
      frames.reserve(frames_json.size());
      for (std::size_t fi = 0; fi < frames_json.size(); ++fi) {
        std::string frame_name;
        try {
          frame_name = frames_json[fi].get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] must be a sprite name",
                                             tileset_path.string(), clip_index, fi));
        }
        const auto local_id = resolve_sprite_id(name_to_local_id, frame_name);
        if (!local_id)
          return std::unexpected(std::format("Tileset '{}' animations.clips[{}] frames[{}] unknown sprite name '{}'",
                                             tileset_path.string(), clip_index, fi, frame_name));
        frames.push_back(*local_id);
      }
      if (frames.empty())
        return std::unexpected(std::format("Tileset '{}' animations.clips[{}] 'frames' must not be empty",
                                           tileset_path.string(), clip_index));
      return frames;
    }

    /// Parse one `animations.clips` entry.
    /// @param clip_index Index of @p clip in the clips array, for error messages.
    /// @param default_fps Clip fps used when the entry does not set its own.
    /// @return The resolved clip, or a message describing the first malformed field.
    std::expected<AnimationClip, std::string>
    parse_animation_clip(const json &clip, std::size_t clip_index, float default_fps, const fs::path &tileset_path,
                         const std::unordered_map<std::string, int> &name_to_local_id) {
      if (!clip.is_object())
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] must be an object", tileset_path.string(), clip_index));

      auto name = parse_clip_name(clip, clip_index, tileset_path);
      if (!name)
        return std::unexpected(std::move(name.error()));

      if (!clip.contains("frames") || !clip["frames"].is_array())
        return std::unexpected(
            std::format("Tileset '{}' animations.clips[{}] missing 'frames' array", tileset_path.string(), clip_index));

      auto fps = parse_clip_fps(clip, clip_index, default_fps, tileset_path);
      if (!fps)
        return std::unexpected(std::move(fps.error()));

      auto frames = parse_clip_frames(clip, clip_index, tileset_path, name_to_local_id);
      if (!frames)
        return std::unexpected(std::move(frames.error()));

      return AnimationClip{.name = std::move(*name), .animation = {.frames = std::move(*frames), .fps = *fps}};
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
        animations[clip->name] = std::move(clip->animation);
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
  std::expected<TilesetInfo, std::string> load_tileset(const core::ContentSource &content,
                                                       const fs::path &tileset_path) {
    auto &cache = tileset_cache();
    const std::string key = tileset_path.string();
    if (const auto it = cache.find(key); it != cache.end())
      return it->second;

    auto atlas = corundum::sprites::load_sprite_atlas(content, tileset_path);
    if (!atlas)
      return std::unexpected(atlas.error());

    auto atlas_json = corundum::core::read_json(content, tileset_path, "tileset");
    if (!atlas_json)
      return std::unexpected(std::move(atlas_json.error()));

    const std::optional<json> sidecar = read_sidecar(content, tileset_path);
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

  namespace {

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

    /// The map-wide values a layer parser needs.
    struct LayerParseContext {
      std::string_view id{};

      int width{0};

      int height{0};

      std::size_t cell_count{0};

      const std::vector<TilemapTileset> *tilesets{nullptr};
    };

    /// A layer's dense tile grid with its sparse animation and flip maps.
    struct LayerTiles {
      std::vector<TileId> tiles{};

      std::flat_map<int, AnimatedCell> animated_cells{};

      std::flat_map<int, uint8_t> flip_flags{};
    };

    /// A layer's name plus its z-order flags.
    struct LayerHeader {
      std::string name{};

      int z_index{0};

      bool depth_sorted{false};
    };

    /// A map's cell dimensions and optional isometric diamond override.
    struct MapDimensions {
      int width{0};

      int height{0};

      int iso_diamond_w{0};

      int iso_diamond_h{0};
    };

    /// The largest full tile frame across every tileset, in pixels.
    struct TileExtent {
      int width{0};

      int height{0};
    };

    /// A cell's integer grid position.
    struct CellPosition {
      int col{0};

      int row{0};
    };

    /// One parsed sparse `objects[]` entry.
    struct SparseObject {
      int col{0};

      int row{0};

      std::size_t index{0};

      TileId tile{k_empty_tile};

      std::uint8_t flip{0};

      bool is_animation{false};

      AnimatedCell animation{};
    };

    /// One parsed `collision_triangles[]` entry.
    struct TriangleShape {
      float col{};

      float row{};

      float col_span{};

      float row_span{};

      TriangleCut cut{};

      std::uint8_t elevation{};
    };

    std::expected<std::string, std::string> parse_tilemap_id(const json &j, const fs::path &path) {
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
      return id;
    }

    /// Parse the optional `collisions` array; absent leaves it empty.
    std::expected<CollisionRects, std::string> parse_collisions(const json &j, const std::string &id) {
      CollisionRects collisions;
      if (!j.contains("collisions"))
        return collisions;
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
        const auto elevation = parse_collision_elevation(entry);
        if (!elevation)
          return std::unexpected(std::format("Tilemap '{}' collisions[{}] {}", id, ci, elevation.error()));
        collisions.push_back(r.col, r.row, r.col_span, r.row_span, *elevation);
      }
      return collisions;
    }

    std::expected<TriangleShape, std::string> parse_triangle(const json &entry, std::size_t index,
                                                             const std::string &id) {
      if (!entry.is_object())
        return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] must be an object", id, index));
      TriangleShape shape;
      std::string cut_str;
      try {
        shape.col = entry.at("x").get<float>();
        shape.row = entry.at("y").get<float>();
        shape.col_span = entry.at("w").get<float>();
        shape.row_span = entry.at("h").get<float>();
        cut_str = entry.at("cut").get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] missing or "
                                           "invalid field (x, y, w, h, cut required)",
                                           id, index));
      }
      if (shape.col_span <= 0.f)
        return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] 'w' must be positive", id, index));
      if (shape.row_span <= 0.f)
        return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] 'h' must be positive", id, index));
      const std::optional<TriangleCut> cut = triangle_cut_from_string(cut_str);
      if (!cut.has_value())
        return std::unexpected(
            std::format("Tilemap '{}' collision_triangles[{}] 'cut' must be NW, NE, SW, or SE", id, index));
      const auto elevation = parse_collision_elevation(entry);
      if (!elevation)
        return std::unexpected(std::format("Tilemap '{}' collision_triangles[{}] {}", id, index, elevation.error()));
      shape.cut = *cut;
      shape.elevation = *elevation;
      return shape;
    }

    /// Parse the optional `collision_triangles` array; absent leaves it empty.
    std::expected<CollisionTriangles, std::string> parse_collision_triangles(const json &j, const std::string &id) {
      CollisionTriangles collision_triangles;
      if (!j.contains("collision_triangles"))
        return collision_triangles;
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
        auto shape = parse_triangle(tri_json[ci], ci, id);
        if (!shape)
          return std::unexpected(std::move(shape.error()));
        collision_triangles.push_back(shape->col, shape->row, shape->col_span, shape->row_span, shape->cut,
                                      shape->elevation);
      }
      return collision_triangles;
    }

    /// Load and validate one `tilesets[]` entry.
    std::expected<TilemapTileset, std::string> parse_tileset(const core::ContentSource &content, const json &entry,
                                                             std::size_t index, const std::string &id) {
      if (!entry.is_object())
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] must be an object", id, index));
      int first_gid = 0;
      std::string source;
      try {
        first_gid = entry.at("first_gid").get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] missing or invalid 'first_gid'", id, index));
      }
      if (first_gid < 0)
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'first_gid' must be >= 0", id, index));
      if (first_gid > static_cast<int>(k_empty_tile) - 1)
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'first_gid'={} exceeds maximum {}", id, index,
                                           first_gid, static_cast<int>(k_empty_tile) - 1));
      try {
        source = entry.at("source").get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] missing or invalid 'source'", id, index));
      }
      if (source.empty())
        return std::unexpected(std::format("Tilemap '{}' tilesets[{}] 'source' must not be empty", id, index));
      auto tileset_result = load_tileset(content, fs::path(source));
      if (!tileset_result.has_value())
        return std::unexpected(std::format("[{}] tilesets[{}]: {}", id, index, tileset_result.error()));
      TilemapTileset ts;
      ts.info = std::move(*tileset_result);
      ts.first_gid = static_cast<TileId>(first_gid);
      if (static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.info.tile_count) >
          static_cast<uint32_t>(k_empty_tile))
        return std::unexpected(
            std::format("Tilemap '{}' tilesets[{}] GID range [{}, {}) exceeds reserved sentinel {}", id, index,
                        ts.first_gid, static_cast<uint32_t>(ts.first_gid) + static_cast<uint32_t>(ts.info.tile_count),
                        static_cast<int>(k_empty_tile)));
      return ts;
    }

    /// Check @p tilesets starts at GID 0 and its sorted ranges are duplicate-free and contiguous.
    std::expected<void, std::string> validate_tileset_ranges(const std::vector<TilemapTileset> &tilesets,
                                                             const std::string &id) {
      if (tilesets[0].first_gid != 0)
        return std::unexpected(std::format("Tilemap '{}' first tileset must have first_gid=0", id));
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
      return {};
    }

    /// Parse the required `tilesets` array, then sort it and check its GID ranges are contiguous.
    std::expected<std::vector<TilemapTileset>, std::string> parse_tilesets(const core::ContentSource &content,
                                                                           const json &j, const std::string &id) {
      if (!j.contains("tilesets") || !j["tilesets"].is_array())
        return std::unexpected(std::format("Tilemap '{}' missing 'tilesets' array", id));
      const auto &tilesets_json = j["tilesets"];
      if (tilesets_json.empty())
        return std::unexpected(std::format("Tilemap '{}' 'tilesets' must not be empty", id));

      std::vector<TilemapTileset> tilesets;
      tilesets.reserve(tilesets_json.size());
      for (std::size_t ti = 0; ti < tilesets_json.size(); ++ti) {
        auto tileset = parse_tileset(content, tilesets_json[ti], ti, id);
        if (!tileset)
          return std::unexpected(std::move(tileset.error()));
        tilesets.push_back(std::move(*tileset));
      }

      std::ranges::sort(tilesets,
                        [](const TilemapTileset &a, const TilemapTileset &b) { return a.first_gid < b.first_gid; });
      if (auto valid = validate_tileset_ranges(tilesets, id); !valid)
        return std::unexpected(std::move(valid.error()));
      return tilesets;
    }

    std::expected<int, std::string> parse_required_dimension(const json &j, const std::string &id, const char *key) {
      if (!j.contains(key))
        return std::unexpected(std::format("Tilemap '{}' missing '{}'", id, key));
      int value = 0;
      try {
        value = j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' field '{}' has wrong type", id, key));
      }
      if (value <= 0)
        return std::unexpected(std::format("Tilemap '{}' field '{}' must be positive", id, key));
      return value;
    }

    std::expected<int, std::string> parse_optional_diamond(const json &j, const std::string &id, const char *key) {
      if (!j.contains(key))
        return 0;
      int value = 0;
      try {
        value = j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' field '{}' has wrong type (expected integer)", id, key));
      }
      if (value <= 0)
        return std::unexpected(std::format("Tilemap '{}' field '{}' must be positive", id, key));
      return value;
    }

    /// Parse the required width/height and the optional isometric diamond override.
    std::expected<MapDimensions, std::string> parse_map_dimensions(const json &j, const std::string &id) {
      auto width = parse_required_dimension(j, id, "width");
      if (!width)
        return std::unexpected(std::move(width.error()));
      auto height = parse_required_dimension(j, id, "height");
      if (!height)
        return std::unexpected(std::move(height.error()));
      auto iso_diamond_w = parse_optional_diamond(j, id, "iso_diamond_w");
      if (!iso_diamond_w)
        return std::unexpected(std::move(iso_diamond_w.error()));
      auto iso_diamond_h = parse_optional_diamond(j, id, "iso_diamond_h");
      if (!iso_diamond_h)
        return std::unexpected(std::move(iso_diamond_h.error()));
      return MapDimensions{
          .width = *width,
          .height = *height,
          .iso_diamond_w = *iso_diamond_w,
          .iso_diamond_h = *iso_diamond_h,
      };
    }

    /// Read a layer's name, clamped z_index and depth_sorted flag.
    std::expected<LayerHeader, std::string> parse_layer_header(const json &layer_json, const LayerParseContext &ctx) {
      if (!layer_json.is_object())
        return std::unexpected(std::format("Tilemap '{}' each layer must be an object", ctx.id));

      LayerHeader header;
      if (!layer_json.contains("name"))
        return std::unexpected(std::format("Tilemap '{}' a layer is missing 'name'", ctx.id));
      try {
        header.name = layer_json["name"].get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' layer 'name' has wrong type", ctx.id));
      }
      if (header.name.empty())
        return std::unexpected(std::format("Tilemap '{}' layer 'name' must not be empty", ctx.id));

      if (layer_json.contains("z_index")) {
        try {
          header.z_index = layer_json["z_index"].get<int>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' field 'z_index' has wrong type", ctx.id, header.name));
        }
        header.z_index = std::max(header.z_index, 0);
      }

      if (layer_json.contains("depth_sorted")) {
        try {
          header.depth_sorted = layer_json["depth_sorted"].get<bool>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' field 'depth_sorted' has wrong type", ctx.id, header.name));
        }
      }
      return header;
    }

    /// Parse a layer's dense `tiles` rows into a flat tile grid.
    std::expected<std::vector<TileId>, std::string>
    parse_dense_layer(const json &layer_json, const std::string &layer_name, const LayerParseContext &ctx) {
      const auto &tiles_json = layer_json["tiles"];
      if (std::cmp_not_equal(tiles_json.size(), ctx.height))
        return std::unexpected(std::format("Tilemap '{}' layer '{}' tiles row count mismatch: expected {}, got {}",
                                           ctx.id, layer_name, ctx.height, tiles_json.size()));

      std::vector<TileId> tiles;
      tiles.reserve(ctx.cell_count);
      for (int r = 0; r < ctx.height; ++r) {
        std::string row_str;
        try {
          row_str = tiles_json[r].get<std::string>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' tiles[{}] must be a string", ctx.id, layer_name, r));
        }
        auto row =
            parse_csv_row(row_str, ctx.width, std::format("Tilemap '{}' layer '{}' row {}", ctx.id, layer_name, r));
        if (!row)
          return std::unexpected(std::move(row.error()));
        for (int col = 0; col < ctx.width; ++col) {
          const int v = (*row)[col];
          if (v < 0)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} col {}: id={} is negative", ctx.id, layer_name, r, col, v));
          if (std::cmp_not_equal(v, k_empty_tile) && find_tileset(*ctx.tilesets, static_cast<TileId>(v)) == nullptr)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' row {} col {}: GID={} not covered by any tileset", ctx.id,
                            layer_name, r, col, v));
          tiles.push_back(static_cast<TileId>(v));
        }
      }
      return tiles;
    }

    std::expected<CellPosition, std::string> parse_sparse_position(const json &entry, std::size_t i,
                                                                   const std::string &layer_name,
                                                                   const LayerParseContext &ctx) {
      CellPosition position;
      try {
        position.col = entry.at("col").get<int>();
        position.row = entry.at("row").get<int>();
      } catch (...) {
        return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] missing or invalid 'col' or 'row'",
                                           ctx.id, layer_name, i));
      }
      if (position.col < 0 || position.col >= ctx.width || position.row < 0 || position.row >= ctx.height)
        return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] position ({}, {}) out of bounds",
                                           ctx.id, layer_name, i, position.col, position.row));
      return position;
    }

    std::expected<std::uint8_t, std::string>
    parse_sparse_flip(const json &entry, std::size_t i, const std::string &layer_name, const LayerParseContext &ctx) {
      if (!entry.contains("flip"))
        return uint8_t{0};
      std::string flip_str;
      try {
        flip_str = entry.at("flip").get<std::string>();
      } catch (...) {
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' objects[{}] 'flip' must be a string", ctx.id, layer_name, i));
      }
      const std::optional<uint8_t> parsed_flip = flip_flags_from_string(flip_str);
      if (!parsed_flip.has_value())
        return std::unexpected(std::format(R"(Tilemap '{}' layer '{}' objects[{}] 'flip' must be "H", "V", or "HV")",
                                           ctx.id, layer_name, i));
      return *parsed_flip;
    }

    std::expected<AnimatedCell, std::string> parse_sparse_animation(const json &entry, std::size_t i,
                                                                    const std::string &layer_name,
                                                                    const LayerParseContext &ctx) {
      std::string anim_name;
      try {
        anim_name = entry.at("anim").get<std::string>();
      } catch (...) {
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' objects[{}] 'anim' must be a string", ctx.id, layer_name, i));
      }
      for (const auto &ts : *ctx.tilesets) {
        const auto it = ts.info.animations.find(anim_name);
        if (it == ts.info.animations.end())
          continue;
        const auto &ta = it->second;
        AnimatedCell cell;
        cell.anim_name = anim_name;
        cell.fps = ta.fps;
        cell.frame_gids.reserve(ta.frames.size());
        for (const int local_id : ta.frames)
          cell.frame_gids.push_back(ts.first_gid + static_cast<TileId>(local_id));
        return cell;
      }
      return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] animation '{}' not found in any tileset",
                                         ctx.id, layer_name, i, anim_name));
    }

    std::expected<TileId, std::string> parse_sparse_tile(const json &entry, std::size_t i,
                                                         const std::string &layer_name, const LayerParseContext &ctx) {
      int tile_id = 0;
      try {
        tile_id = entry.at("id").get<int>();
      } catch (...) {
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' objects[{}] missing or invalid 'id'", ctx.id, layer_name, i));
      }
      if (tile_id < 0 || find_tileset(*ctx.tilesets, static_cast<TileId>(tile_id)) == nullptr)
        return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] GID={} not covered by any tileset",
                                           ctx.id, layer_name, i, tile_id));
      return static_cast<TileId>(tile_id);
    }

    /// Resolve one sparse `objects[]` entry to its cell and its tile or animation.
    std::expected<SparseObject, std::string>
    parse_sparse_object(const json &entry, std::size_t i, const std::string &layer_name, const LayerParseContext &ctx) {
      if (!entry.is_object())
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' objects[{}] must be an object", ctx.id, layer_name, i));

      const bool has_id = entry.contains("id");
      const bool has_anim = entry.contains("anim");
      if (has_id && has_anim)
        return std::unexpected(std::format("Tilemap '{}' layer '{}' objects[{}] must have 'id' or 'anim', not both",
                                           ctx.id, layer_name, i));
      if (!has_id && !has_anim)
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' objects[{}] missing 'id' or 'anim'", ctx.id, layer_name, i));

      auto position = parse_sparse_position(entry, i, layer_name, ctx);
      if (!position)
        return std::unexpected(std::move(position.error()));
      SparseObject object{.col = position->col, .row = position->row};
      object.index = cell_index(ctx.width, object.col, object.row);

      if (has_anim) {
        auto animation = parse_sparse_animation(entry, i, layer_name, ctx);
        if (!animation)
          return std::unexpected(std::move(animation.error()));
        object.is_animation = true;
        object.animation = std::move(*animation);
        return object;
      }
      auto flip = parse_sparse_flip(entry, i, layer_name, ctx);
      if (!flip)
        return std::unexpected(std::move(flip.error()));
      auto tile = parse_sparse_tile(entry, i, layer_name, ctx);
      if (!tile)
        return std::unexpected(std::move(tile.error()));
      object.flip = *flip;
      object.tile = *tile;
      return object;
    }

    /// Parse a layer's sparse `objects` list into a flat tile grid.
    std::expected<LayerTiles, std::string> parse_sparse_layer(const json &layer_json, const std::string &layer_name,
                                                              const LayerParseContext &ctx) {
      LayerTiles tiles{.tiles = std::vector<TileId>(ctx.cell_count, k_empty_tile)};
      std::vector<bool> occupied(ctx.cell_count, false);
      const auto &objects = layer_json["objects"];
      for (std::size_t i = 0; i < objects.size(); ++i) {
        auto object = parse_sparse_object(objects[i], i, layer_name, ctx);
        if (!object)
          return std::unexpected(std::move(object.error()));
        if (occupied[object->index])
          return std::unexpected(std::format("Tilemap '{}' layer '{}' duplicate entry at ({}, {})", ctx.id, layer_name,
                                             object->col, object->row));
        occupied[object->index] = true;
        if (object->is_animation) {
          tiles.animated_cells[static_cast<int>(object->index)] = std::move(object->animation);
          continue;
        }
        tiles.tiles[object->index] = object->tile;
        if (object->flip != 0)
          tiles.flip_flags[static_cast<int>(object->index)] = object->flip;
      }
      return tiles;
    }

    /// Parse a layer's dense tile grid or sparse object list.
    std::expected<LayerTiles, std::string> parse_layer_tiles(const json &layer_json, const std::string &layer_name,
                                                             const LayerParseContext &ctx) {
      const bool has_dense = layer_json.contains("tiles") && layer_json["tiles"].is_array();
      const bool has_sparse = layer_json.contains("objects") && layer_json["objects"].is_array();
      if (has_dense && has_sparse)
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' must have 'tiles' or 'objects', not both", ctx.id, layer_name));
      if (has_dense) {
        auto tiles = parse_dense_layer(layer_json, layer_name, ctx);
        if (!tiles)
          return std::unexpected(std::move(tiles.error()));
        return LayerTiles{.tiles = std::move(*tiles)};
      }
      if (has_sparse)
        return parse_sparse_layer(layer_json, layer_name, ctx);
      return std::unexpected(std::format(
          "Tilemap '{}' layer '{}' must have either 'tiles' (dense) or 'objects' (sparse)", ctx.id, layer_name));
    }

    /// Parse a layer's optional elevation grid (same rows-of-CSV layout as its tiles).
    std::expected<std::vector<std::uint8_t>, std::string>
    parse_layer_elevation(const json &layer_json, const std::string &layer_name, const LayerParseContext &ctx) {
      std::vector<std::uint8_t> elevation;
      if (!layer_json.contains("elevation"))
        return elevation;
      const auto &elev_json = layer_json["elevation"];
      if (!elev_json.is_array())
        return std::unexpected(
            std::format("Tilemap '{}' layer '{}' field 'elevation' must be an array", ctx.id, layer_name));
      if (std::cmp_not_equal(elev_json.size(), ctx.height))
        return std::unexpected(std::format("Tilemap '{}' layer '{}' elevation row count mismatch: expected {}, got {}",
                                           ctx.id, layer_name, ctx.height, elev_json.size()));
      elevation.reserve(ctx.cell_count);
      for (int r = 0; r < ctx.height; ++r) {
        std::string row_str;
        try {
          row_str = elev_json[r].get<std::string>();
        } catch (...) {
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' elevation[{}] must be a string", ctx.id, layer_name, r));
        }
        auto row = parse_csv_row(row_str, ctx.width,
                                 std::format("Tilemap '{}' layer '{}' elevation row {}", ctx.id, layer_name, r));
        if (!row)
          return std::unexpected(std::move(row.error()));
        for (int col = 0; col < ctx.width; ++col) {
          const int v = (*row)[col];
          if (v < 0 || v > 255)
            return std::unexpected(
                std::format("Tilemap '{}' layer '{}' elevation row {} col {}: value {} out of range [0, 255]", ctx.id,
                            layer_name, r, col, v));
          elevation.push_back(static_cast<uint8_t>(v));
        }
      }
      return elevation;
    }

    /// Parse a layer's optional per-cell terrain-material overrides.
    std::expected<std::flat_map<int, std::string>, std::string>
    parse_material_overrides(const json &layer_json, const std::string &layer_name, const LayerParseContext &ctx) {
      std::flat_map<int, std::string> material_overrides;
      if (!layer_json.contains("material_overrides") || !layer_json["material_overrides"].is_array())
        return material_overrides;
      const auto &mat_json = layer_json["material_overrides"];
      for (std::size_t mi = 0; mi < mat_json.size(); ++mi) {
        const auto &entry = mat_json[mi];
        if (!entry.is_object())
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' material_overrides[{}] must be an object", ctx.id, layer_name, mi));
        int mcol = 0;
        int mrow = 0;
        std::string material;
        try {
          mcol = entry.at("col").get<int>();
          mrow = entry.at("row").get<int>();
          material = entry.at("material").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format(
              "Tilemap '{}' layer '{}' material_overrides[{}] missing or invalid 'col', 'row', or 'material'", ctx.id,
              layer_name, mi));
        }
        if (mcol < 0 || mcol >= ctx.width || mrow < 0 || mrow >= ctx.height)
          return std::unexpected(std::format("Tilemap '{}' layer '{}' material_overrides[{}] position ({}, {}) "
                                             "out of bounds",
                                             ctx.id, layer_name, mi, mcol, mrow));
        const std::size_t flat_idx = cell_index(ctx.width, mcol, mrow);
        if (flat_idx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
          return std::unexpected(std::format("Tilemap '{}' layer '{}' material_overrides[{}] index {} "
                                             "exceeds int maximum",
                                             ctx.id, layer_name, mi, flat_idx));
        material_overrides[static_cast<int>(flat_idx)] = std::move(material);
      }
      return material_overrides;
    }

    /// Parse a layer's optional per-cell ramp axes.
    std::expected<std::flat_map<int, RampAxis>, std::string>
    parse_ramps(const json &layer_json, const std::string &layer_name, const LayerParseContext &ctx) {
      std::flat_map<int, RampAxis> ramps;
      if (!layer_json.contains("ramps") || !layer_json["ramps"].is_array())
        return ramps;
      const auto &ramps_json = layer_json["ramps"];
      for (std::size_t ri = 0; ri < ramps_json.size(); ++ri) {
        const auto &entry = ramps_json[ri];
        if (!entry.is_object())
          return std::unexpected(
              std::format("Tilemap '{}' layer '{}' ramps[{}] must be an object", ctx.id, layer_name, ri));
        int rcol = 0;
        int rrow = 0;
        std::string axis_str;
        try {
          rcol = entry.at("col").get<int>();
          rrow = entry.at("row").get<int>();
          axis_str = entry.at("axis").get<std::string>();
        } catch (const nlohmann::json::exception &) {
          return std::unexpected(std::format(
              "Tilemap '{}' layer '{}' ramps[{}] missing or invalid 'col', 'row', or 'axis'", ctx.id, layer_name, ri));
        }
        if (rcol < 0 || rcol >= ctx.width || rrow < 0 || rrow >= ctx.height)
          return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] position ({}, {}) out of bounds",
                                             ctx.id, layer_name, ri, rcol, rrow));
        const std::optional<RampAxis> axis = ramp_axis_from_string(axis_str);
        if (!axis.has_value())
          return std::unexpected(std::format(R"(Tilemap '{}' layer '{}' ramps[{}] axis '{}' must be "ns" or "ew")",
                                             ctx.id, layer_name, ri, axis_str));
        const std::size_t flat_idx = cell_index(ctx.width, rcol, rrow);
        if (flat_idx > static_cast<std::size_t>(std::numeric_limits<int>::max()))
          return std::unexpected(std::format("Tilemap '{}' layer '{}' ramps[{}] index {} exceeds int maximum", ctx.id,
                                             layer_name, ri, flat_idx));
        ramps[static_cast<int>(flat_idx)] = *axis;
      }
      return ramps;
    }

    /// Parse one layer from its header, tile data and optional elevation, materials and ramps.
    std::expected<TilemapLayer, std::string> parse_layer(const json &layer_json, const LayerParseContext &ctx) {
      auto header = parse_layer_header(layer_json, ctx);
      if (!header)
        return std::unexpected(std::move(header.error()));

      auto tiles = parse_layer_tiles(layer_json, header->name, ctx);
      if (!tiles)
        return std::unexpected(std::move(tiles.error()));
      auto elevation = parse_layer_elevation(layer_json, header->name, ctx);
      if (!elevation)
        return std::unexpected(std::move(elevation.error()));
      auto material_overrides = parse_material_overrides(layer_json, header->name, ctx);
      if (!material_overrides)
        return std::unexpected(std::move(material_overrides.error()));
      auto ramps = parse_ramps(layer_json, header->name, ctx);
      if (!ramps)
        return std::unexpected(std::move(ramps.error()));

      return TilemapLayer{
          .name = std::move(header->name),
          .z_index = header->z_index,
          .depth_sorted = header->depth_sorted,
          .visible = true,
          .tiles = std::move(tiles->tiles),
          .animated_cells = std::move(tiles->animated_cells),
          .flip_flags = std::move(tiles->flip_flags),
          .elevation = std::move(*elevation),
          .material_overrides = std::move(*material_overrides),
          .ramps = std::move(*ramps),
      };
    }

    /// Validate the required `layers` array and parse every layer in it.
    std::expected<std::vector<TilemapLayer>, std::string> parse_layers(const json &j, const std::string &id, int width,
                                                                       int height,
                                                                       const std::vector<TilemapTileset> &tilesets) {
      if (!j.contains("layers") || !j["layers"].is_array())
        return std::unexpected(std::format("Tilemap '{}' missing 'layers' array", id));
      const auto &layers_json = j["layers"];
      if (layers_json.empty())
        return std::unexpected(std::format("Tilemap '{}' must have at least one layer", id));

      const std::size_t cell_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
      // flat_map keys and the animation/flip indices are ints, so a cell count above INT_MAX would
      // silently wrap those indices; reject it rather than overflow them.
      if (cell_count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(
            std::format("Tilemap '{}' dimensions {}x{} exceed the supported cell count", id, width, height));

      const LayerParseContext ctx{
          .id = id,
          .width = width,
          .height = height,
          .cell_count = cell_count,
          .tilesets = &tilesets,
      };
      std::vector<TilemapLayer> layers;
      layers.reserve(layers_json.size());
      for (const auto &layer_json : layers_json) {
        auto layer = parse_layer(layer_json, ctx);
        if (!layer)
          return std::unexpected(std::move(layer.error()));
        layers.push_back(std::move(*layer));
      }
      return layers;
    }

    /// The largest full tile frame across @p tilesets, in pixels.
    TileExtent max_tile_extent(const std::vector<TilemapTileset> &tilesets) {
      TileExtent extent;
      for (const auto &ts : tilesets) {
        if (!ts.info.tile_full_width.empty())
          extent.width = std::max(extent.width, *std::ranges::max_element(ts.info.tile_full_width));
        if (!ts.info.tile_full_height.empty())
          extent.height = std::max(extent.height, *std::ranges::max_element(ts.info.tile_full_height));
      }
      return extent;
    }

  } // namespace

  std::expected<Tilemap, std::string> load_tilemap(const core::ContentSource &content, const fs::path &path) {
    auto parsed = corundum::core::read_json(content, path, "tilemap");
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    json &j = *parsed;

    auto prepared = core::prepare_schema_version(j, k_tilemap_schema_version, "Tilemap", path.string(), {});
    if (!prepared)
      return std::unexpected(prepared.error());

    auto parsed_id = parse_tilemap_id(j, path);
    if (!parsed_id)
      return std::unexpected(std::move(parsed_id.error()));
    const std::string &id = *parsed_id;

    auto collisions = parse_collisions(j, id);
    if (!collisions)
      return std::unexpected(std::move(collisions.error()));

    auto collision_triangles = parse_collision_triangles(j, id);
    if (!collision_triangles)
      return std::unexpected(std::move(collision_triangles.error()));

    auto tilesets = parse_tilesets(content, j, id);
    if (!tilesets)
      return std::unexpected(std::move(tilesets.error()));

    auto dimensions = parse_map_dimensions(j, id);
    if (!dimensions)
      return std::unexpected(std::move(dimensions.error()));

    auto layers = parse_layers(j, id, dimensions->width, dimensions->height, *tilesets);
    if (!layers)
      return std::unexpected(std::move(layers.error()));

    const TileExtent extent = max_tile_extent(*tilesets);

    return Tilemap{
        .path = path.string(),
        .tilesets = std::move(*tilesets),
        .width = dimensions->width,
        .height = dimensions->height,
        .iso_diamond_w = dimensions->iso_diamond_w,
        .iso_diamond_h = dimensions->iso_diamond_h,
        .layers = std::move(*layers),
        .collisions = std::move(*collisions),
        .collision_triangles = std::move(*collision_triangles),
        .max_tile_full_w = extent.width,
        .max_tile_full_h = extent.height,
    };
  }

} // namespace corundum::world::tilemap
