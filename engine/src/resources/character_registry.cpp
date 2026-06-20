#include <corundum/core/files.hpp>
#include <corundum/resources/character_registry.hpp>
#include <corundum/resources/sprite.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::resources {

  std::expected<void, std::string> CharacterRegistry::load_all(const fs::path &index_path) {
    const fs::path characters_dir = index_path / "characters";

    // Recurse so character sheets can be organized into subdirectories per character.
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(characters_dir, ec)) {
      if (!ec && entry.is_regular_file() && entry.path().extension() == ".json")
        files.push_back(entry.path());
    }

    if (files.empty())
      return std::unexpected(std::format("Characters sprite sheets are missing: {}", characters_dir.string()));

    for (const auto &file : files) {
      auto result = load_sheet(fs::path(file));
      if (!result)
        return std::unexpected(result.error());
    }

    // Populate sprite_by_id_ after all load_sheet() calls complete
    sprite_by_id_.resize(next_sprite_id_, nullptr);
    for (auto &&[sprite_name, frames] : frames_) {
      const auto sid = get_sprite_id(sprite_name);
      if (sid != k_null_sprite_id) {
        sprite_by_id_[sid] = &frames;
      }
    }

    return {};
  }

  std::expected<void, std::string> CharacterRegistry::load_sheet(const fs::path &sheet_path) {
    std::ifstream f(sheet_path);
    if (!f)
      return std::unexpected(std::format("Cannot open sheet: {}", sheet_path.string()));

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed sheet {}: {}", sheet_path.string(), e.what()));
    }

    auto require_str = [&](const char *key) -> std::expected<std::string, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sheet '{}' missing '{}'", sheet_path.string(), key));
      try {
        return j[key].get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("Sheet field '{}' has wrong type", key));
      }
    };

    auto require_int = [&](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sheet '{}' missing '{}'", sheet_path.string(), key));
      try {
        return j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Sheet field '{}' has wrong type", key));
      }
    };

    auto id_result = require_str("id");
    if (!id_result)
      return std::unexpected(id_result.error());
    const auto id = *id_result;

    auto path_result = require_str("path");
    if (!path_result)
      return std::unexpected(path_result.error());
    const auto path_str = *path_result;

    auto fw_result = require_int("frame_width");
    if (!fw_result)
      return std::unexpected(fw_result.error());
    const int frame_width = *fw_result;

    auto fh_result = require_int("frame_height");
    if (!fh_result)
      return std::unexpected(fh_result.error());
    const int frame_height = *fh_result;

    const int offset_x = j.value("offset_x", 0);
    const int offset_y = j.value("offset_y", 0);
    const int spacing_x = j.value("spacing_x", 0);
    const int spacing_y = j.value("spacing_y", 0);

    if (!j.contains("frames") || !j["frames"].is_object())
      return std::unexpected(std::format("Sprite Sheet '{}' missing 'sprites' object", id));

    if (sheet_ids_.contains(id))
      return std::unexpected(std::format("Duplicate sheet id: '{}'", id));

    const Id sheet_id = next_id_++;
    sheet_ids_.emplace(id, sheet_id);
    sheets_by_id_.emplace(
        sheet_id, SpriteSheet{sheet_id, path_str, frame_width, frame_height, offset_x, offset_y, spacing_x, spacing_y});

    for (const auto &[sprite_name, anims_json] : j["frames"].items()) {
      if (!anims_json.is_object())
        return std::unexpected(std::format("Sprite '{}' must be an object of animations", sprite_name));

      if (frames_.contains(sprite_name))
        return std::unexpected(std::format("Duplicate sprite name: '{}'", sprite_name));

      Frames data;
      data.sheet_id = sheet_id;
      data.col_span = anims_json.value("col_span", 1);
      data.row_span = anims_json.value("row_span", 1);
      if (data.col_span < 1)
        return std::unexpected(std::format("Sprite '{}' col_span must be >= 1", sprite_name));
      if (data.row_span < 1)
        return std::unexpected(std::format("Sprite '{}' row_span must be >= 1", sprite_name));
      data.collision_w = anims_json.value("collision_w", 0);
      data.collision_h = anims_json.value("collision_h", 0);
      data.walk_around_offset = anims_json.value("walk_around_offset", 0.f);
      data.fps = anims_json.value("fps", 0.f);
      data.sprite_id = next_sprite_id_++;
      data.anim_frames.fill({});

      for (const auto &[anim_name, frames_json] : anims_json.items()) {
        if (anim_name == "walk_around_offset" || anim_name == "col_span" || anim_name == "row_span" ||
            anim_name == "fps" || anim_name == "collision_w" || anim_name == "collision_h")
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

        const auto aid = anim_name_to_id(anim_name);
        if (aid != AnimId::COUNT)
          data.anim_frames[static_cast<uint8_t>(aid)] = coords;

        data.animations.emplace(anim_name, std::move(coords));
      }

      frames_.emplace(sprite_name, std::move(data));
    }

    return {};
  }

  Id CharacterRegistry::find_sheet(const std::string &id) const noexcept {
    auto it = sheet_ids_.find(id);
    return it != sheet_ids_.end() ? it->second : k_null_sheet;
  }

  const SpriteSheet *CharacterRegistry::get_sheet(Id id) const noexcept {
    auto it = sheets_by_id_.find(id);
    return it != sheets_by_id_.end() ? &it->second : nullptr;
  }

  const Frames *CharacterRegistry::get_sprite(const std::string &sprite_name) const noexcept {
    auto it = frames_.find(sprite_name);
    return it != frames_.end() ? &it->second : nullptr;
  }

  SpriteId CharacterRegistry::get_sprite_id(const std::string &sprite_name) const noexcept {
    auto it = frames_.find(sprite_name);
    return it != frames_.end() ? it->second.sprite_id : k_null_sprite_id;
  }

  const Frames *CharacterRegistry::get_sprite_by_id(SpriteId sid) const noexcept {
    if (sid == k_null_sprite_id)
      return nullptr;
    if (sid >= sprite_by_id_.size())
      return nullptr;
    return sprite_by_id_[sid];
  }

} // namespace corundum::resources
