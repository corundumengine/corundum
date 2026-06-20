#pragma once
#include "editor_state.hpp"
#include <filesystem>
#include <stdexcept>

namespace tools::sprite {

  /**
   * @brief Exception thrown when a sprite sheet JSON is malformed.
   */
  struct SheetLoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  /**
   * @brief Load a sprite sheet JSON file into @p state.
   *
   * Detects Character vs Sprite Sheet mode automatically from the JSON structure.
   * Character sheets have a "frames" object; sprite sheet sheets have "columns".
   *
   * @param state Output editor state to populate.
   * @param path  Path to the sprite sheet JSON file.
   * @throws SheetLoadError on malformed JSON schema.
   * @throws std::runtime_error if the file cannot be opened.
   */
  void load_sheet(EditorState &state, const std::filesystem::path &path);

} // namespace tools::sprite
