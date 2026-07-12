#pragma once
#include <corundum/resources/sprite_sheet_clips.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::resources {

  /** @brief Load a sprite-sheet clips JSON file.
   *
   * Accepts a missing schema_version field as version 1.
   *
   * @param[in] path  Path to the sprite-sheet clips JSON.
   * @return SpriteSheetClips on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<SpriteSheetClips, std::string> load_sprite_sheet_clips(const std::filesystem::path &path);

} // namespace corundum::resources
