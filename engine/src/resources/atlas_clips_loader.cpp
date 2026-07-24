#include <corundum/resources/atlas_clips_loader.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

namespace corundum::resources {

  std::expected<AtlasClipsData, std::string> load_atlas_clips(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open atlas clips sidecar: {}", path.string()));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed atlas clips sidecar {}: {}", path.string(), e.what()));
    }

    if (!j.contains("schema_version"))
      return std::unexpected(std::format("Atlas clips sidecar '{}' missing 'schema_version'", path.string()));
    if (!j["schema_version"].is_number_integer())
      return std::unexpected(
          std::format("Atlas clips sidecar '{}' field 'schema_version' has wrong type", path.string()));
    const int schema_version = j["schema_version"].get<int>();
    if (schema_version != k_atlas_clips_schema_version)
      return std::unexpected(std::format("Atlas clips sidecar '{}' has schema_version {}, but this engine expects {}",
                                         path.string(), schema_version, k_atlas_clips_schema_version));

    AtlasClipsData data;
    if (!j.contains("clips"))
      return data;

    const auto &clips_arr = j["clips"];
    if (!clips_arr.is_array())
      return std::unexpected(std::format("Atlas clips sidecar '{}' 'clips' must be an array", path.string()));

    std::unordered_set<std::string> seen_names;
    data.clips.reserve(clips_arr.size());

    for (const auto &cj : clips_arr) {
      AtlasClip clip;
      if (!cj.contains("name") || !cj["name"].is_string())
        return std::unexpected(std::format("Atlas clips sidecar '{}' clip missing 'name'", path.string()));
      clip.name = cj["name"].get<std::string>();
      if (clip.name.empty())
        return std::unexpected(std::format("Atlas clips sidecar '{}' has a clip with empty 'name'", path.string()));
      if (!seen_names.insert(clip.name).second)
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' has duplicate clip name '{}'", path.string(), clip.name));

      clip.fps = cj.value("fps", 8);
      if (clip.fps <= 0)
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' clip '{}' has non-positive 'fps'", path.string(), clip.name));

      if (!cj.contains("frames") || !cj["frames"].is_array())
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' clip '{}' missing 'frames' array", path.string(), clip.name));

      for (const auto &fj : cj["frames"]) {
        if (!fj.is_string())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has a non-string frame entry", path.string(), clip.name));
        std::string frame_name = fj.get<std::string>();
        if (frame_name.empty())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has an empty frame name", path.string(), clip.name));
        clip.frames.push_back(std::move(frame_name));
      }

      data.clips.push_back(std::move(clip));
    }

    return data;
  }

} // namespace corundum::resources
