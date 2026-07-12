#include <corundum/resources/sprite_sheet_clips_serializer.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::resources {

  json serialize_sprite_sheet_clips(const SpriteSheetClips &data) {
    json j;
    j["schema_version"] = 1;
    j["id"] = data.id;
    j["columns"] = data.columns;
    j["rows"] = data.rows;
    j["path"] = data.path;
    j["frame_width"] = data.frame_width;
    j["frame_height"] = data.frame_height;
    if (data.offset_x)
      j["offset_x"] = data.offset_x;
    if (data.offset_y)
      j["offset_y"] = data.offset_y;
    if (data.spacing_x)
      j["spacing_x"] = data.spacing_x;
    if (data.spacing_y)
      j["spacing_y"] = data.spacing_y;

    if (!data.clips.empty()) {
      json aj;
      aj["fps"] = data.anim_fps;
      aj["clips"] = json::array();
      for (const auto &clip : data.clips) {
        json cj;
        cj["name"] = clip.name;
        cj["frames"] = json::array();
        for (const auto &fc : clip.frames)
          cj["frames"].push_back({{"col", fc.col}, {"row", fc.row}});
        aj["clips"].push_back(cj);
      }
      j["animations"] = aj;
    }
    return j;
  }

} // namespace corundum::resources
