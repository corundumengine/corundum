// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/sprite_sheet_clips.hpp>
#include <corundum/sprites/sprite_sheet_clips_serializer.hpp>
#include <nlohmann/json_fwd.hpp>

#include <nlohmann/json.hpp>
#include <utility>

using nlohmann::json;

namespace corundum::sprites {

  json serialize_sprite_sheet_clips(const SpriteSheetClips &data) {
    json j;
    j["schema_version"] = k_sprite_sheet_clips_schema_version;
    j["id"] = data.id;
    j["columns"] = data.columns;
    j["rows"] = data.rows;
    j["path"] = data.path;
    j["frame_width"] = data.frame_width;
    j["frame_height"] = data.frame_height;
    if (data.offset_x != 0)
      j["offset_x"] = data.offset_x;
    if (data.offset_y != 0)
      j["offset_y"] = data.offset_y;
    if (data.spacing_x != 0)
      j["spacing_x"] = data.spacing_x;
    if (data.spacing_y != 0)
      j["spacing_y"] = data.spacing_y;

    // Emit "animations" whenever it carries information: clips, or a non-default fps that would
    // otherwise be dropped on the next load.
    if (!data.clips.empty() || data.anim_fps != k_default_anim_fps) {
      json animations;
      animations["fps"] = data.anim_fps;
      animations["clips"] = json::array();
      for (const auto &clip : data.clips) {
        json cj;
        cj["name"] = clip.name;
        cj["frames"] = json::array();
        for (const auto &fc : clip.frames)
          cj["frames"].push_back({{"col", fc.col}, {"row", fc.row}});
        animations["clips"].push_back(std::move(cj));
      }
      j["animations"] = std::move(animations);
    }
    return j;
  }

} // namespace corundum::sprites
