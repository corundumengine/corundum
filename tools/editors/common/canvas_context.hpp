#pragma once
#include <imgui.h>

namespace tools::common {

  /// Drawing context for the editor canvas.
  ///
  /// Passed to all canvas render functions so they can emit draw calls in
  /// screen space without knowing the canvas window position themselves.
  /// All canvas-local coordinates (x, y = 0 at top-left of canvas) must be
  /// offset by @p origin before issuing DrawList calls.
  struct CanvasContext {
    ImDrawList *dl; ///< Draw list for the canvas child window.
    ImVec2 origin;  ///< Screen-space position of the canvas top-left corner.
  };

} // namespace tools::common
