// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <corundum/sprites/sprite.hpp>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::sprites {

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
    float walk_around_offset = k_default_walk_around_offset; ///< Fraction of sprite height locating the feet.
    float fps = 0.f;
    std::array<std::vector<FrameCoord>, k_num_anim_ids> anim_frames; ///< Hot-path frame layout indexed by AnimId.
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

  /// Load and validate a character sheet JSON file. Every animation key under a sprite must
  /// name a known AnimId; an unrecognized key is rejected rather than silently dropped.
  /// @param path Path to the character sheet JSON.
  /// @return CharacterSheetData on success, or an error string on failure.
  [[nodiscard]] std::expected<CharacterSheetData, std::string> load_character_sheet(const std::filesystem::path &path);

} // namespace corundum::sprites
