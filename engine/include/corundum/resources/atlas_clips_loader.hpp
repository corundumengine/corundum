#pragma once
#include <corundum/resources/atlas_clips.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::resources {

  /** @brief Load and validate an atlas clips sidecar JSON file.
   *
   * @param path  Path to the `<stem>.spritedata.json` sidecar.
   * @return AtlasClipsData on success, or an error string on any parse or validation failure.
   */
  [[nodiscard]] std::expected<AtlasClipsData, std::string> load_atlas_clips(const std::filesystem::path &path);

} // namespace corundum::resources
