#include <corundum/resources/character_sheet_serializer.hpp>
#include <corundum/resources/sprite.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace corundum::resources;

namespace corundum::resources {

  json serialize_character_sheet(const CharacterSheetData &data) {
    json j;
    j["id"] = data.id;
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

    j["frames"] = json::object();
    for (const auto &sp : data.sprites) {
      json sj;
      sj["col_span"] = sp.col_span;
      sj["row_span"] = sp.row_span;
      if (sp.collision_w > 0)
        sj["collision_w"] = sp.collision_w;
      if (sp.collision_h > 0)
        sj["collision_h"] = sp.collision_h;
      sj["walk_around_offset"] = sp.walk_around_offset;
      if (sp.fps > 0.f)
        sj["fps"] = sp.fps;
      for (uint8_t i = 0; i < k_num_anim_ids; ++i) {
        if (sp.anim_frames[i].empty())
          continue;
        auto &arr = sj[std::string(k_anim_names[i])] = json::array();
        for (const auto &fc : sp.anim_frames[i])
          arr.push_back({{"col", fc.col}, {"row", fc.row}});
      }
      j["frames"][sp.name] = sj;
    }
    return j;
  }

} // namespace corundum::resources
