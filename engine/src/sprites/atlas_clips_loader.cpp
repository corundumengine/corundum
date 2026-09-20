// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/atlas_clips.hpp>
#include <corundum/sprites/atlas_clips_loader.hpp>
#include <nlohmann/json_fwd.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

using nlohmann::json;

namespace corundum::sprites {

  namespace {

    /** @brief Validate and parse one entry of the sidecar's "clips" array.
     *
     * @param clip_json  The clip object; all field types are checked before extraction.
     * @param path       Sidecar path, used only to name the file in error messages.
     */
    std::expected<AtlasClip, std::string> parse_clip(const json &clip_json, const std::filesystem::path &path) {
      AtlasClip clip;

      if (!clip_json.contains("name") || !clip_json["name"].is_string())
        return std::unexpected(std::format("Atlas clips sidecar '{}' clip missing 'name'", path.string()));
      clip.name = clip_json["name"].get<std::string>();
      if (clip.name.empty())
        return std::unexpected(std::format("Atlas clips sidecar '{}' has a clip with empty 'name'", path.string()));

      if (clip_json.contains("fps")) {
        if (!clip_json["fps"].is_number_integer())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' field 'fps' has wrong type", path.string(), clip.name));
        clip.fps = clip_json["fps"].get<int>();
        if (clip.fps <= 0)
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has non-positive 'fps'", path.string(), clip.name));
      }

      if (!clip_json.contains("frames") || !clip_json["frames"].is_array())
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' clip '{}' missing 'frames' array", path.string(), clip.name));

      for (const auto &frame_json : clip_json["frames"]) {
        if (!frame_json.is_string())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has a non-string frame entry", path.string(), clip.name));
        std::string frame_name = frame_json.get<std::string>();
        if (frame_name.empty())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has an empty frame name", path.string(), clip.name));
        clip.frames.push_back(std::move(frame_name));
      }

      return clip;
    }

  } // namespace

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
      auto clip = parse_clip(cj, path);
      if (!clip)
        return std::unexpected(clip.error());

      if (!seen_names.insert(clip->name).second)
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' has duplicate clip name '{}'", path.string(), clip->name));

      data.clips.push_back(std::move(*clip));
    }

    return data;
  }

} // namespace corundum::sprites
