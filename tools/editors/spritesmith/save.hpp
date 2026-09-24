// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include <expected>
#include <string>

namespace tools::spritesmith {

  /**
   * @brief Serialize the editor state to @p state.json_path.
   *
   * Writes the character or sprite sheet JSON format depending on state.mode.
   * In Atlas mode, state.json_path names the (read-only) atlas file; only its
   * authored clips sidecar (`<stem>.spritedata.json`) is ever written.
   * Requires state.json_path to be non-empty.
   * Sets state.dirty = false on success.
   *
   * @param state Editor state to serialize.
   * @return Empty expected on success; error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> save_sheet(EditorState &state);

  /**
   * @brief Open the Save As browser, pre-filled with a sensible default filename.
   */
  void open_save_as_browser(EditorState &state);

  /**
   * @brief Save if state.json_path is set; otherwise open the Save As browser instead of erroring.
   */
  void action_save(EditorState &state);

} // namespace tools::spritesmith
