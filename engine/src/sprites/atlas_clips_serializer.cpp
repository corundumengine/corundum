// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/atlas_clips.hpp>
#include <corundum/sprites/atlas_clips_serializer.hpp>
#include <nlohmann/json_fwd.hpp>
#include <utility>

using nlohmann::json;

namespace corundum::sprites {

  json serialize_atlas_clips(const AtlasClipsData &data) {
    json j;
    j["schema_version"] = k_atlas_clips_schema_version;
    j["clips"] = json::array();
    for (const auto &clip : data.clips) {
      json cj;
      cj["name"] = clip.name;
      cj["fps"] = clip.fps;
      cj["frames"] = clip.frames;
      j["clips"].push_back(std::move(cj));
    }
    return j;
  }

} // namespace corundum::sprites
