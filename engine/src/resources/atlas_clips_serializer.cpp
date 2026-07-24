#include <corundum/resources/atlas_clips_serializer.hpp>

using json = nlohmann::json;

namespace corundum::resources {

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

} // namespace corundum::resources
