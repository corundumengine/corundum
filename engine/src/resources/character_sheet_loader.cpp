#include <algorithm>
#include <array>
#include <corundum/resources/character_sheet_loader.hpp>
#include <corundum/resources/sprite.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::resources {

  std::expected<CharacterSheetData, std::string> load_character_sheet(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open sheet: {}", path.string()));

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed sheet {}: {}", path.string(), e.what()));
    }

    auto require_str = [&j, &path](const char *key) -> std::expected<std::string, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sheet '{}' missing '{}'", path.string(), key));
      try {
        return j[key].get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Sheet field '{}' has wrong type", key));
      }
    };

    auto require_int = [&j, &path](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sheet '{}' missing '{}'", path.string(), key));
      try {
        return j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Sheet field '{}' has wrong type", key));
      }
    };

    auto id_result = require_str("id");
    if (!id_result)
      return std::unexpected(id_result.error());

    auto path_result = require_str("path");
    if (!path_result)
      return std::unexpected(path_result.error());

    auto fw_result = require_int("frame_width");
    if (!fw_result)
      return std::unexpected(fw_result.error());

    auto fh_result = require_int("frame_height");
    if (!fh_result)
      return std::unexpected(fh_result.error());

    CharacterSheetData data;
    data.id = *id_result;
    data.path = *path_result;
    data.frame_width = *fw_result;
    data.frame_height = *fh_result;
    data.offset_x = j.value("offset_x", 0);
    data.offset_y = j.value("offset_y", 0);
    data.spacing_x = j.value("spacing_x", 0);
    data.spacing_y = j.value("spacing_y", 0);

    if (!j.contains("frames") || !j["frames"].is_object())
      return std::unexpected(std::format("Sprite Sheet '{}' missing 'frames' object", data.id));

    for (const auto &[sprite_name, anims_json] : j["frames"].items()) {
      if (!anims_json.is_object())
        return std::unexpected(std::format("Sprite '{}' must be an object of animations", sprite_name));

      CharacterSpriteEntry entry;
      entry.name = sprite_name;
      entry.col_span = anims_json.value("col_span", 1);
      entry.row_span = anims_json.value("row_span", 1);
      if (entry.col_span < 1)
        return std::unexpected(std::format("Sprite '{}' col_span must be >= 1", sprite_name));
      if (entry.row_span < 1)
        return std::unexpected(std::format("Sprite '{}' row_span must be >= 1", sprite_name));
      entry.collision_w = anims_json.value("collision_w", 0);
      entry.collision_h = anims_json.value("collision_h", 0);
      entry.walk_around_offset = anims_json.value("walk_around_offset", 0.6f);
      entry.fps = anims_json.value("fps", 0.f);
      entry.anim_frames.fill({});

      static constexpr auto k_metadata_keys = std::to_array<std::string_view>({
          "walk_around_offset",
          "col_span",
          "row_span",
          "fps",
          "collision_w",
          "collision_h",
      });

      for (const auto &[anim_name, frames_json] : anims_json.items()) {
        if (std::ranges::contains(k_metadata_keys, anim_name))
          continue;
        if (!frames_json.is_array() || frames_json.empty())
          return std::unexpected(std::format("Animation '{}/{}' must be a non-empty array", sprite_name, anim_name));

        std::vector<FrameCoord> coords;
        coords.reserve(frames_json.size());

        for (const auto &frame : frames_json) {
          try {
            coords.push_back({frame.at("col").get<int>(), frame.at("row").get<int>()});
          } catch (...) {
            return std::unexpected(std::format("Frame in '{}/{}' missing 'col' or 'row'", sprite_name, anim_name));
          }
        }

        const AnimId aid = anim_name_to_id(anim_name);
        if (aid != AnimId::COUNT)
          entry.anim_frames[static_cast<uint8_t>(aid)] = coords;

        entry.animations.emplace(anim_name, std::move(coords));
      }

      data.sprites.push_back(std::move(entry));
    }

    return data;
  }

} // namespace corundum::resources
