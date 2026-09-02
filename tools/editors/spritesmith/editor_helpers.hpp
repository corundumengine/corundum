#pragma once
#include "editor_state.hpp"

namespace tools::spritesmith {

  /**
   * @brief Number of frame columns in the current grid.
   *
   * Sprite sheets use the authored state.columns; character sheets derive the
   * column count from the image footprint, offsets, and spacing.
   *
   * @param state Editor state to query.
   * @return Column count, or 0 when no grid fits.
   */
  [[nodiscard]] inline int sheet_cols(const EditorState &state) {
    if (state.mode == SheetMode::SpriteSheet)
      return state.columns;
    const int step = state.frame_width + state.spacing_x;
    if (step <= 0 || state.image_pixel_w <= state.offset_x)
      return 0;
    return (state.image_pixel_w - state.offset_x + state.spacing_x) / step;
  }

  /**
   * @brief Number of frame rows in the current grid.
   *
   * Sprite sheets use the authored state.rows; character sheets derive the row
   * count from the image footprint, offsets, and spacing.
   *
   * @param state Editor state to query.
   * @return Row count, or 0 when no grid fits.
   */
  [[nodiscard]] inline int sheet_rows(const EditorState &state) {
    if (state.mode == SheetMode::SpriteSheet)
      return state.rows;
    const int step = state.frame_height + state.spacing_y;
    if (step <= 0 || state.image_pixel_h <= state.offset_y)
      return 0;
    return (state.image_pixel_h - state.offset_y + state.spacing_y) / step;
  }

  /**
   * @brief True when the user is actively recording frames in the current mode.
   *
   * @param state Editor state to query.
   * @return Recording flag for the active sheet mode.
   */
  [[nodiscard]] inline bool is_recording(const EditorState &state) {
    return (state.mode == SheetMode::Character && state.anim_recording) ||
           (state.mode == SheetMode::SpriteSheet && state.clip_recording) ||
           (state.mode == SheetMode::Atlas && state.atlas_clip_recording);
  }

} // namespace tools::spritesmith
