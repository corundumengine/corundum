// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_io.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/sprites/sprite_sheet_clips.hpp>
#include <corundum/sprites/sprite_sheet_clips_loader.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

using nlohmann::json;

namespace corundum::sprites {

  namespace {

    /// Migration step for sprite-sheet clips documents. No migrations exist yet — schema_version 1
    /// is both the legacy (absent-field) form and the current format — so this returns @p
    /// from_version unchanged. Future steps advance @p from_version and are never edited once
    /// shipped; leaving this stub unchanged after a version bump fails loudly (the caller rejects a
    /// result below the current version).
    std::expected<int, std::string> migrate_sprite_sheet_clips_json(json & /*root*/, int from_version,
                                                                    const std::string & /*file*/) {
      return from_version;
    }

    /// Read a required integer field as int, rejecting a missing field, a wrong type, or a value
    /// outside int range. Values are checked here rather than left to nlohmann, whose get<int>()
    /// throws on a wrong type and narrows an out-of-range integer.
    std::expected<int, std::string> require_int(const json &object, const char *key, const std::string &file) {
      if (!object.contains(key))
        return std::unexpected(std::format("Sprite sheet '{}' missing '{}'", file, key));
      if (!object[key].is_number_integer())
        return std::unexpected(std::format("Sprite sheet '{}' field '{}' must be an integer", file, key));

      const std::int64_t value = object[key].get<std::int64_t>();
      if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
        return std::unexpected(std::format("Sprite sheet '{}' field '{}' is out of range", file, key));
      return static_cast<int>(value);
    }

    /// Read an optional integer field, returning @p fallback when the field is absent.
    std::expected<int, std::string> optional_int(const json &object, const char *key, int fallback,
                                                 const std::string &file) {
      if (!object.contains(key))
        return fallback;
      return require_int(object, key, file);
    }

    /// Parse one "animations.clips" entry, checking every field's type and range. An empty
    /// "frames" array is allowed so a clip being authored in spritesmith survives a save/load.
    std::expected<AnimClip, std::string> parse_clip(const json &clip_json, const std::string &file) {
      if (!clip_json.is_object())
        return std::unexpected(std::format("Sprite sheet '{}' has a non-object clip entry", file));

      if (!clip_json.contains("name") || !clip_json["name"].is_string())
        return std::unexpected(std::format("Sprite sheet '{}' clip missing string 'name'", file));

      AnimClip clip;
      clip.name = clip_json["name"].get<std::string>();
      if (clip.name.empty())
        return std::unexpected(std::format("Sprite sheet '{}' has a clip with empty 'name'", file));

      if (!clip_json.contains("frames") || !clip_json["frames"].is_array())
        return std::unexpected(std::format("Sprite sheet '{}' clip '{}' missing 'frames' array", file, clip.name));

      clip.frames.reserve(clip_json["frames"].size());
      for (const auto &frame_json : clip_json["frames"]) {
        if (!frame_json.is_object())
          return std::unexpected(
              std::format("Sprite sheet '{}' clip '{}' has a non-object frame entry", file, clip.name));

        auto col = require_int(frame_json, "col", file);
        if (!col)
          return std::unexpected(std::move(col).error());
        auto row = require_int(frame_json, "row", file);
        if (!row)
          return std::unexpected(std::move(row).error());
        if (*col < 0 || *row < 0)
          return std::unexpected(std::format("Sprite sheet '{}' clip '{}' frame has negative col/row ({}, {})", file,
                                             clip.name, *col, *row));

        clip.frames.push_back({.col = *col, .row = *row});
      }

      return clip;
    }

    /// Read the optional, non-negative frame-placement fields (offset_x/y, spacing_x/y), which
    /// share a shape but land in distinct members. Defaults to 0 for each absent field.
    std::expected<void, std::string> read_placement(const json &root, SpriteSheetClips &data, const std::string &file) {
      for (const auto &[key, member] : std::array{
               std::pair{"offset_x", &SpriteSheetClips::offset_x},
               std::pair{"offset_y", &SpriteSheetClips::offset_y},
               std::pair{"spacing_x", &SpriteSheetClips::spacing_x},
               std::pair{"spacing_y", &SpriteSheetClips::spacing_y},
           }) {
        auto value = optional_int(root, key, 0, file);
        if (!value)
          return std::unexpected(std::move(value).error());
        if (*value < 0)
          return std::unexpected(std::format("Sprite sheet '{}' field '{}' must be >= 0", file, key));
        data.*member = *value;
      }
      return {};
    }

    /// Parse and validate the optional "animations" block into @p data.
    std::expected<void, std::string> read_animations(const json &root, SpriteSheetClips &data,
                                                     const std::string &file) {
      if (!root.contains("animations"))
        return {};

      const auto &animations = root["animations"];
      if (!animations.is_object())
        return std::unexpected(std::format("Sprite sheet '{}' 'animations' must be an object", file));

      auto fps = optional_int(animations, "fps", k_default_anim_fps, file);
      if (!fps)
        return std::unexpected(std::move(fps).error());
      if (*fps <= 0)
        return std::unexpected(std::format("Sprite sheet '{}' field 'animations.fps' must be > 0", file));
      data.anim_fps = *fps;

      if (!animations.contains("clips"))
        return {};

      const auto &clips_arr = animations["clips"];
      if (!clips_arr.is_array())
        return std::unexpected(std::format("Sprite sheet '{}' field 'animations.clips' must be an array", file));

      std::unordered_set<std::string> seen_names;
      data.clips.reserve(clips_arr.size());
      for (const auto &clip_json : clips_arr) {
        auto clip = parse_clip(clip_json, file);
        if (!clip)
          return std::unexpected(std::move(clip).error());
        if (!seen_names.insert(clip->name).second)
          return std::unexpected(std::format("Sprite sheet '{}' has duplicate clip name '{}'", file, clip->name));
        data.clips.push_back(std::move(*clip));
      }

      return {};
    }

  } // namespace

  std::expected<SpriteSheetClips, std::string> load_sprite_sheet_clips(const std::filesystem::path &path) {
    const std::string file = path.string();

    auto j_result = core::read_json(path, "sprite sheet");
    if (!j_result)
      return std::unexpected(std::move(j_result).error());
    json j = std::move(*j_result);

    if (!j.is_object())
      return std::unexpected(std::format("Sprite sheet '{}' must be a JSON object", file));

    auto prepared = core::prepare_schema_version(j, k_sprite_sheet_clips_schema_version, "Sprite sheet", file,
                                                 migrate_sprite_sheet_clips_json);
    if (!prepared)
      return std::unexpected(std::move(prepared).error());

    SpriteSheetClips data;

    if (j.contains("id")) {
      if (!j["id"].is_string())
        return std::unexpected(std::format("Sprite sheet '{}' field 'id' must be a string", file));
      data.id = j["id"].get<std::string>();
    }

    if (!j.contains("path") || !j["path"].is_string())
      return std::unexpected(std::format("Sprite sheet '{}' missing 'path'", file));
    data.path = j["path"].get<std::string>();

    auto frame_width = require_int(j, "frame_width", file);
    if (!frame_width)
      return std::unexpected(std::move(frame_width).error());
    auto frame_height = require_int(j, "frame_height", file);
    if (!frame_height)
      return std::unexpected(std::move(frame_height).error());
    auto columns = require_int(j, "columns", file);
    if (!columns)
      return std::unexpected(std::move(columns).error());
    auto rows = require_int(j, "rows", file);
    if (!rows)
      return std::unexpected(std::move(rows).error());

    if (*frame_width <= 0)
      return std::unexpected(std::format("Sprite sheet '{}' frame_width must be > 0", file));
    if (*frame_height <= 0)
      return std::unexpected(std::format("Sprite sheet '{}' frame_height must be > 0", file));
    if (*columns < 0)
      return std::unexpected(std::format("Sprite sheet '{}' columns must be >= 0", file));
    if (*rows < 0)
      return std::unexpected(std::format("Sprite sheet '{}' rows must be >= 0", file));

    data.frame_width = *frame_width;
    data.frame_height = *frame_height;
    data.columns = *columns;
    data.rows = *rows;

    if (auto placement = read_placement(j, data, file); !placement)
      return std::unexpected(std::move(placement).error());

    if (auto animations = read_animations(j, data, file); !animations)
      return std::unexpected(std::move(animations).error());

    return data;
  }

} // namespace corundum::sprites
