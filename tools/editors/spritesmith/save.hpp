#pragma once
#include "editor_state.hpp"
#include <expected>
#include <string>

namespace tools::sprite {

  /**
   * @brief Serialize the editor state to @p state.json_path.
   *
   * Writes the character or sprite sheet JSON format depending on state.mode.
   * Requires state.json_path to be non-empty.
   * Sets state.dirty = false on success.
   *
   * @param state Editor state to serialize.
   * @return Empty expected on success; error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> save_sheet(EditorState &state);

} // namespace tools::sprite
