#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/canvas_controller.hpp>

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

  /**
   * @brief Draw a line across every ramp cell's own axis, on the active layer.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_ramps_overlay(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Draw a preview line on the hovered tile using the currently selected ramp axis.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_ramp_preview(CanvasContext ctx, const EditorState &state);

} // namespace tools::tilemap
