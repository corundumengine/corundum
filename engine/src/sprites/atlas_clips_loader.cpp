// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/schema_version.hpp>
#include <corundum/sprites/atlas_clips.hpp>
#include <corundum/sprites/atlas_clips_loader.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

using nlohmann::json;

namespace corundum::sprites {

  namespace {

    /// Migrates an atlas clips sidecar in place up to k_atlas_clips_schema_version, returning the
    /// version reached. No migrations exist yet — schema_version 1 is both the legacy (absent-field)
    /// format and the current format — so this returns @p from_version unchanged. Future steps
    /// advance @p from_version and are never edited once shipped; leaving this stub unchanged after
    /// a version bump fails loudly (the caller rejects a result below the current version).
    std::expected<int, std::string> migrate_atlas_clips_json(json & /*j*/, int from_version,
                                                             const std::string & /*path*/) {
      return from_version;
    }

    /** @brief Validate and parse one entry of the sidecar's "clips" array.
     *
     * @param clip_json  The clip object; all field types are checked before extraction.
     * @param file       Sidecar file name, used only to identify the file in error messages.
     */
    std::expected<AtlasClip, std::string> parse_clip(const json &clip_json, const std::string &file) {
      AtlasClip clip;

      if (!clip_json.contains("name") || !clip_json["name"].is_string())
        return std::unexpected(std::format("Atlas clips sidecar '{}' clip missing 'name'", file));
      clip.name = clip_json["name"].get<std::string>();
      if (clip.name.empty())
        return std::unexpected(std::format("Atlas clips sidecar '{}' has a clip with empty 'name'", file));

      if (clip_json.contains("fps")) {
        if (!clip_json["fps"].is_number_integer())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' field 'fps' has wrong type", file, clip.name));
        const std::int64_t fps = clip_json["fps"].get<std::int64_t>();
        if (fps <= 0)
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has non-positive 'fps'", file, clip.name));
        if (fps > std::numeric_limits<int>::max())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has 'fps' out of range", file, clip.name));
        clip.fps = static_cast<int>(fps);
      }

      if (!clip_json.contains("frames") || !clip_json["frames"].is_array())
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' clip '{}' missing 'frames' array", file, clip.name));
      if (clip_json["frames"].empty())
        return std::unexpected(
            std::format("Atlas clips sidecar '{}' clip '{}' has an empty 'frames' array", file, clip.name));

      for (const auto &frame_json : clip_json["frames"]) {
        if (!frame_json.is_string())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has a non-string frame entry", file, clip.name));
        std::string frame_name = frame_json.get<std::string>();
        if (frame_name.empty())
          return std::unexpected(
              std::format("Atlas clips sidecar '{}' clip '{}' has an empty frame name", file, clip.name));
        clip.frames.push_back(std::move(frame_name));
      }

      return clip;
    }

  } // namespace

  std::expected<AtlasClipsData, std::string> load_atlas_clips(const std::filesystem::path &path) {
    const std::string file = path.string();

    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open atlas clips sidecar: {}", file));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed atlas clips sidecar {}: {}", file, e.what()));
    }

    auto prepared = core::prepare_schema_version(j, k_atlas_clips_schema_version, "Atlas clips sidecar", file,
                                                 migrate_atlas_clips_json);
    if (!prepared)
      return std::unexpected(std::move(prepared).error());

    AtlasClipsData data;
    if (!j.contains("clips"))
      return data;

    const auto &clips_arr = j["clips"];
    if (!clips_arr.is_array())
      return std::unexpected(std::format("Atlas clips sidecar '{}' 'clips' must be an array", file));

    std::unordered_set<std::string> seen_names;
    data.clips.reserve(clips_arr.size());

    for (const auto &cj : clips_arr) {
      auto clip = parse_clip(cj, file);
      if (!clip)
        return std::unexpected(clip.error());

      if (!seen_names.insert(clip->name).second)
        return std::unexpected(std::format("Atlas clips sidecar '{}' has duplicate clip name '{}'", file, clip->name));

      data.clips.push_back(std::move(*clip));
    }

    return data;
  }

} // namespace corundum::sprites
