#pragma once
#include "common/canvas_context.hpp"
#include "editor_state.hpp"

namespace tools::tilemap {

  using CanvasContext = tools::common::CanvasContext;

  /**
   * @brief Render a translucent color-coded tint over every non-flat cell on
   * the active layer.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_elevation_overlay(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Render a highlighted diamond at the hovered cell showing the brush value.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_elevation_preview(CanvasContext ctx, const EditorState &state);

} // namespace tools::tilemap
