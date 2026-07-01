#pragma once
#include <array>
#include <corundum/resources/sprite.hpp>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <string>
#include <vector>

namespace corundum::resources {

  /// One sprite entry parsed from a character sheet's "frames" object.
  struct CharacterSpriteEntry {
    std::string name;
    int col_span = 1;
    int row_span = 1;
    int collision_w = 0;
    int collision_h = 0;
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

} // namespace corundum::resources
