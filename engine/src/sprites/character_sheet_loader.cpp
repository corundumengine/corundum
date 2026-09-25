// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <corundum/core/json_io.hpp>
#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::sprites {

  namespace {

    /// Reads a required key as T, converting a missing key or a wrong-typed value into an error
    /// string rather than letting nlohmann throw out of the load path.
    template <typename T>
    std::expected<T, std::string> required_value(const json &object, const char *key, const fs::path &path) {
      if (!object.contains(key))
        return std::unexpected(std::format("Sheet '{}' missing '{}'", path.string(), key));
      try {
        return object.at(key).get<T>();
      } catch (const json::exception &e) {
        return std::unexpected(std::format("Sheet '{}' field '{}': {}", path.string(), key, e.what()));
      }
    }

    /// Parse one "frames" entry. Metadata keys are read first; every remaining key must resolve
    /// to an AnimId, because the runtime only ever plays the direction-indexed anim_frames and a
    /// misspelled clip would otherwise load without error and do nothing.
    std::expected<CharacterSpriteEntry, std::string> parse_sprite_entry(const std::string &sprite_name,
                                                                        const json &anims_json, const fs::path &path) {
      CharacterSpriteEntry entry;
      entry.name = sprite_name;

      try {
        entry.col_span = anims_json.value("col_span", 1);
        entry.row_span = anims_json.value("row_span", 1);
        entry.footprint_authored =
            anims_json.contains("footprint_col_span") && anims_json.contains("footprint_row_span");
        entry.footprint_col_span = anims_json.value("footprint_col_span", k_default_footprint_col_span);
        entry.footprint_row_span = anims_json.value("footprint_row_span", k_default_footprint_row_span);
        entry.walk_around_offset = anims_json.value("walk_around_offset", k_default_walk_around_offset);
        entry.fps = anims_json.value("fps", 0.f);
      } catch (const json::exception &e) {
        return std::unexpected(std::format("Sheet '{}' sprite '{}': {}", path.string(), sprite_name, e.what()));
      }

      if (entry.col_span < 1)
        return std::unexpected(std::format("Sprite '{}' col_span must be >= 1", sprite_name));
      if (entry.row_span < 1)
        return std::unexpected(std::format("Sprite '{}' row_span must be >= 1", sprite_name));
      if (entry.footprint_col_span < 0.f || entry.footprint_row_span < 0.f)
        return std::unexpected(std::format("Sprite '{}' footprint spans must be >= 0", sprite_name));
      if (entry.fps < 0.f)
        return std::unexpected(std::format("Sprite '{}' fps must be >= 0", sprite_name));

      static constexpr auto k_metadata_keys = std::to_array<std::string_view>({
          "walk_around_offset",
          "col_span",
          "row_span",
          "fps",
          "footprint_col_span",
          "footprint_row_span",
      });

      for (const auto &[anim_name, frames_json] : anims_json.items()) {
        if (std::ranges::contains(k_metadata_keys, anim_name))
          continue;

        const AnimId anim_id = anim_name_to_id(anim_name);
        if (anim_id == AnimId::Count)
          return std::unexpected(std::format("Sprite '{}' has unknown animation '{}'", sprite_name, anim_name));

        if (!frames_json.is_array() || frames_json.empty())
          return std::unexpected(std::format("Animation '{}/{}' must be a non-empty array", sprite_name, anim_name));

        std::vector<FrameCoord> coords;
        coords.reserve(frames_json.size());
        for (const auto &frame : frames_json) {
          try {
            coords.push_back({.col = frame.at("col").get<int>(), .row = frame.at("row").get<int>()});
          } catch (const json::exception &e) {
            return std::unexpected(std::format("Frame in '{}/{}' is invalid: {}", sprite_name, anim_name, e.what()));
          }
        }

        entry.anim_frames[static_cast<uint8_t>(anim_id)] = std::move(coords);
      }

      return entry;
    }

  } // namespace

  std::expected<CharacterSheetData, std::string> load_character_sheet(const fs::path &path) {
    auto j_result = core::read_json(path, "sheet");
    if (!j_result)
      return std::unexpected(std::move(j_result).error());
    json j = std::move(*j_result);

    const auto id = required_value<std::string>(j, "id", path);
    if (!id)
      return std::unexpected(id.error());

    const auto image_path = required_value<std::string>(j, "path", path);
    if (!image_path)
      return std::unexpected(image_path.error());

    const auto frame_width = required_value<int>(j, "frame_width", path);
    if (!frame_width)
      return std::unexpected(frame_width.error());

    const auto frame_height = required_value<int>(j, "frame_height", path);
    if (!frame_height)
      return std::unexpected(frame_height.error());

    if (*frame_width <= 0)
      return std::unexpected(std::format("Sheet '{}' frame_width must be > 0", path.string()));
    if (*frame_height <= 0)
      return std::unexpected(std::format("Sheet '{}' frame_height must be > 0", path.string()));

    CharacterSheetData data;
    data.id = *id;
    data.path = *image_path;
    data.frame_width = *frame_width;
    data.frame_height = *frame_height;

    try {
      data.offset_x = j.value("offset_x", 0);
      data.offset_y = j.value("offset_y", 0);
      data.spacing_x = j.value("spacing_x", 0);
      data.spacing_y = j.value("spacing_y", 0);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Sheet '{}' has an invalid field: {}", path.string(), e.what()));
    }

    if (!j.contains("frames") || !j.at("frames").is_object())
      return std::unexpected(std::format("Sheet '{}' missing 'frames' object", path.string()));

    for (const auto &[sprite_name, anims_json] : j.at("frames").items()) {
      if (!anims_json.is_object())
        return std::unexpected(std::format("Sprite '{}' must be an object of animations", sprite_name));

      auto entry = parse_sprite_entry(sprite_name, anims_json, path);
      if (!entry)
        return std::unexpected(entry.error());

      data.sprites.push_back(std::move(*entry));
    }

    return data;
  }

} // namespace corundum::sprites
