#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/canvas_controller.hpp>

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

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
