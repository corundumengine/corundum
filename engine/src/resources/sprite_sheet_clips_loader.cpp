#include <corundum/resources/sprite_sheet_clips_loader.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::resources {

  std::expected<SpriteSheetClips, std::string> load_sprite_sheet_clips(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open sprite sheet: {}", path.string()));

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed sprite sheet {}: {}", path.string(), e.what()));
    }

    SpriteSheetClips data;
    data.id = j.value("id", std::string{});

    if (!j.contains("path") || !j["path"].is_string())
      return std::unexpected(std::format("Sprite sheet '{}' missing 'path'", path.string()));
    data.path = j["path"].get<std::string>();

    auto require_int = [&j, &path](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sprite sheet '{}' missing '{}'", path.string(), key));
      try {
        return j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Sprite sheet '{}' field '{}' has wrong type", path.string(), key));
      }
    };

    auto fw = require_int("frame_width");
    if (!fw)
      return std::unexpected(fw.error());
    data.frame_width = *fw;

    auto fh = require_int("frame_height");
    if (!fh)
      return std::unexpected(fh.error());
    data.frame_height = *fh;

    auto cols = require_int("columns");
    if (!cols)
      return std::unexpected(cols.error());
    data.columns = *cols;

    auto rows = require_int("rows");
    if (!rows)
      return std::unexpected(rows.error());
    data.rows = *rows;

    data.offset_x = j.value("offset_x", 0);
    data.offset_y = j.value("offset_y", 0);
    data.spacing_x = j.value("spacing_x", 0);
    data.spacing_y = j.value("spacing_y", 0);

    if (!j.contains("animations"))
      return data;

    const auto &aj = j["animations"];
    if (!aj.is_object())
      return std::unexpected(std::format("Sprite sheet '{}' 'animations' must be an object", path.string()));

    data.anim_fps = aj.value("fps", 2);

    if (!aj.contains("clips"))
      return data;

    const auto &clips_arr = aj["clips"];
    if (!clips_arr.is_array())
      return std::unexpected(std::format("Sprite sheet '{}' 'animations.clips' must be an array", path.string()));

    for (const auto &cj : clips_arr) {
      AnimClip clip;
      if (!cj.contains("name"))
        return std::unexpected(std::format("Sprite sheet '{}' clip missing 'name'", path.string()));
      clip.name = cj["name"].get<std::string>();

      if (!cj.contains("frames") || !cj["frames"].is_array())
        return std::unexpected(
            std::format("Sprite sheet '{}' clip '{}' missing 'frames' array", path.string(), clip.name));

      for (const auto &fc : cj["frames"]) {
        if (!fc.contains("col") || !fc.contains("row"))
          return std::unexpected(
              std::format("Sprite sheet '{}' clip '{}' frame missing 'col' or 'row'", path.string(), clip.name));
        clip.frames.push_back({fc["col"].get<int>(), fc["row"].get<int>()});
      }
      data.clips.push_back(std::move(clip));
    }

    return data;
  }

} // namespace corundum::resources
