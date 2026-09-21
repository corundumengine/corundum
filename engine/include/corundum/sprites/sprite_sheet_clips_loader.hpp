// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/sprites/sprite_sheet_clips.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::sprites {

  /** @brief Load a sprite-sheet clips JSON file.
   *
   * A missing schema_version field is read as version 1; a newer version is rejected. Every
   * field is type- and range-checked, so a malformed file returns an error rather than throwing.
   *
   * @param[in] path  Path to the sprite-sheet clips JSON.
   * @return SpriteSheetClips on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<SpriteSheetClips, std::string> load_sprite_sheet_clips(const std::filesystem::path &path);

} // namespace corundum::sprites
