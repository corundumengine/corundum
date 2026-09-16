// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <corundum/sprites/sprite.hpp>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <string>
#include <vector>

namespace corundum::sprites {

  /// Footprint a sprite falls back to when its sheet omits one, in tile-grid units. Named
  /// rather than written inline so the load path can report the substitution instead of
  /// leaving a silent default that has nothing to do with the sprite's art.
  inline constexpr float k_default_footprint_col_span = 0.25f;

  inline constexpr float k_default_footprint_row_span = 0.5f;

  /// One sprite entry parsed from a character sheet's "frames" object.
  struct CharacterSpriteEntry {
    std::string name;
    int col_span = 1;
    int row_span = 1;
    float footprint_col_span = k_default_footprint_col_span; ///< Collision footprint width, tile-grid units.
    float footprint_row_span = k_default_footprint_row_span; ///< Collision footprint depth, tile-grid units.
    /// False when the sheet omitted either footprint key, so the caller can report that the
    /// defaults above were substituted.
    bool footprint_authored = false;
    float walk_around_offset = 0.6f;
    float fps = 0.f;
    std::flat_map<std::string, std::vector<FrameCoord>> animations;
    std::array<std::vector<FrameCoord>, k_num_anim_ids> anim_frames;
  };

  /// Top-level data parsed from a character sheet JSON file.
  struct CharacterSheetData {
    std::string id;
    std::string path;
    int frame_width = 0;
    int frame_height = 0;
    int offset_x = 0;
    int offset_y = 0;
    int spacing_x = 0;
    int spacing_y = 0;
    std::vector<CharacterSpriteEntry> sprites;
  };

  /// Load and validate a character sheet JSON file.
  /// @param path Path to the character sheet JSON.
  /// @return CharacterSheetData on success, or an error string on failure.
  [[nodiscard]] std::expected<CharacterSheetData, std::string> load_character_sheet(const std::filesystem::path &path);

} // namespace corundum::sprites
