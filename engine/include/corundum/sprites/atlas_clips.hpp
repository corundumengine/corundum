// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::sprites {

  /** @brief Atlas clips sidecar JSON schema version. */
  inline constexpr int k_atlas_clips_schema_version = 1;

  /** @brief Clip playback rate applied when the sidecar omits "fps". */
  inline constexpr int k_default_clip_fps = 8;

  /** @brief One named animation clip over a sprite atlas.
   *
   * References frames by stable sprite name so clips survive repacking
   * (rects/indices don't).
   */
  struct AtlasClip {
    /** @brief Playback rate in frames per second; must be positive. */
    int fps = k_default_clip_fps;

    /** @brief Ordered AtlasSprite names. */
    std::vector<std::string> frames;

    /** @brief Clip identifier, unique within the sidecar. */
    std::string name;
  };

  /** @brief Authored animation data for a sprite atlas.
   *
   * Loaded from its `<stem>.spritedata.json` sidecar.
   */
  struct AtlasClipsData {
    std::vector<AtlasClip> clips;
  };

  /** @brief Compute the `.spritedata.json` sidecar path for an atlas JSON.
   *
   * @param atlas_path  Path to the atlas JSON file; its extension is replaced.
   * @return Sidecar path, e.g. `ground-d.json` → `ground-d.spritedata.json`.
   */
  [[nodiscard]] std::filesystem::path atlas_clips_sidecar_path(std::filesystem::path atlas_path);

} // namespace corundum::sprites
