#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/canvas_controller.hpp>

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

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
