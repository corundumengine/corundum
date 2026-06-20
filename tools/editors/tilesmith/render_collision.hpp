#pragma once
#include "common/canvas_context.hpp"
#include "editor_state.hpp"

namespace tools::tilemap {

  using CanvasContext = tools::common::CanvasContext;

  /**
   * @brief Render collision rectangles and triangles as semi-transparent overlays.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_collisions(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Render a live preview of the collision rect being dragged.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_collision_preview(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Render a live preview of the tile rect being erased via right-click drag.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_erase_preview(CanvasContext ctx, const EditorState &state);

} // namespace tools::tilemap
