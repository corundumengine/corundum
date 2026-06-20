#pragma once
#include <imgui.h>

namespace tools::common {

  /// ImGui font handles loaded at startup.
  ///
  /// Separated from EditorState so that pure-data headers do not transitively
  /// depend on ImGui. Constructed after ImGui font loading and passed explicitly
  /// to the render functions that need fonts.
  struct FontHandles {
    ImFont *ui{};    ///< Default proportional UI font.
    ImFont *icons{}; ///< Icon glyph font (visibility toggles, etc.).
  };

} // namespace tools::common
