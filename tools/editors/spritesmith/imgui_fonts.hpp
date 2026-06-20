#pragma once
#include "common/imgui_fonts.hpp"

namespace tools::sprite {

  /**
   * @brief ImGui font handles used by Spritesmith render functions.
   *
   * Separated from EditorState so that pure-data headers do not transitively
   * depend on ImGui. Constructed after ImGui font loading and passed explicitly
   * to the render functions that need fonts.
   */
  using FontHandles = tools::common::FontHandles;

} // namespace tools::sprite
