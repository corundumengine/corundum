#pragma once
#include "common/canvas_context.hpp"
#include "editor_state.hpp"

namespace tools::tilemap {

  using CanvasContext = tools::common::CanvasContext;

  /**
   * @brief Render portal rectangles as semi-transparent teal overlays.
   *
   * The selected portal is drawn with a brighter border and a small label
   * showing the target map stem and spawn coordinates.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_portals(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Render a live preview of the portal rect being dragged.
   *
   * @param ctx    Canvas draw context.
   * @param state  Current editor state.
   */
  void render_portal_preview(CanvasContext ctx, const EditorState &state);

  /**
   * @brief Render the ImGui portal edit panel (popup modal).
   *
   * No-op when show_portals is false or no portal is selected.
   *
   * @param state  Editor state to read and modify.
   */
  void render_portal_panel(EditorState &state);

} // namespace tools::tilemap
