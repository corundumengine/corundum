#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/canvas_controller.hpp>
#include <functional>

namespace tools::tilemap {

  using CanvasContext = corundum::tool_host::CanvasContext;

  /// Injected callback that renders all tilemap passes into the canvas.
  /// Constructed in main.cpp, which owns TilemapTextureStore.
  using MapRenderFn = std::function<void(CanvasContext)>;

  /**
   * @brief Render the tilemap canvas including collision and portal overlays.
   *
   * Calls @p render_map to draw the tilemap passes (provided by main.cpp as a
   * closure over TilemapTextureStore), then draws editor overlays.
   *
   * @param ctx         Canvas draw context (draw list + screen-space origin).
   * @param state       Current editor state.
   * @param render_map  Callback that issues all tilemap draw calls into @p ctx.
   */
  void render_canvas(CanvasContext ctx, const EditorState &state, const MapRenderFn &render_map);

} // namespace tools::tilemap
