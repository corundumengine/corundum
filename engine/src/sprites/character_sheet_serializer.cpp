// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/character_sheet_serializer.hpp>
#include <corundum/sprites/sprite.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

using nlohmann::json;

namespace corundum::sprites {

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
      // Write a footprint only when one was authored. Emitting the substituted defaults would
      // flip footprint_authored on the next load and silence the "no footprint" registry warning.
      if (sp.footprint_authored) {
        sj["footprint_col_span"] = sp.footprint_col_span;
        sj["footprint_row_span"] = sp.footprint_row_span;
      }
      if (sp.walk_around_offset != k_default_walk_around_offset)
        sj["walk_around_offset"] = sp.walk_around_offset;
      if (sp.fps > 0.f)
        sj["fps"] = sp.fps;
      for (uint8_t i = 0; i < k_num_anim_ids; ++i) {
        if (sp.anim_frames[i].empty())
          continue;
        json &arr = sj[std::string(k_anim_names[i])] = json::array();
        for (const auto &fc : sp.anim_frames[i])
          arr.push_back({{"col", fc.col}, {"row", fc.row}});
      }
      j["frames"][sp.name] = sj;
    }
    return j;
  }

} // namespace corundum::sprites
