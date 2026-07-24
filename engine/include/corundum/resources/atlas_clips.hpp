#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::resources {

  /** @brief Atlas clips sidecar JSON schema version. */
  inline constexpr int k_atlas_clips_schema_version = 1;

  /** @brief One named animation clip over a sprite atlas.
   *
   * References frames by stable sprite name so clips survive repacking
   * (rects/indices don't).
   */
  struct AtlasClip {
    std::string name;
    int fps = 8;
    std::vector<std::string> frames; ///< Ordered AtlasSprite names.
  };

  /** @brief Authored animation data for a sprite atlas.
   *
   * Loaded from its `<stem>.spritedata.json` sidecar.
   */
  struct AtlasClipsData {
    std::vector<AtlasClip> clips;
  };

  /** @brief Compute the spritedata sidecar path for an atlas JSON.
   *
   * Replaces the extension to produce `<stem>.spritedata.json`.
   *
   * @param atlas_path  Path to the atlas JSON file.
   * @return Sidecar path with `.spritedata.json` extension.
   */
  [[nodiscard]] std::filesystem::path atlas_clips_sidecar_path(const std::filesystem::path &atlas_path);

} // namespace corundum::resources
