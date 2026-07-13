#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/canvas_controller.hpp>

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

  /**
   * @brief Render a highlighted line on every disconnected cardinal walkability
   * edge (the boundary between two adjacent tiles whose elevation delta exceeds
   * max_step_height).
   *
   * Diagonal edges aren't drawn — resolve_walkability() only ever tests cardinal
   * (N/S/E/W) edges today, so a diagonal line would imply a gameplay consequence
   * that doesn't exist yet. The graph is rebuilt fresh every call (cheap even for
   * large maps) so this stays live as elevation is painted.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_walkability_overlay(CanvasContext ctx, const EditorState &state);

} // namespace tools::tilemap
