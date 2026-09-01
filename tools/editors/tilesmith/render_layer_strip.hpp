#pragma once
#include "editor_state.hpp"
#include <corundum/tool_host/ui_theme.hpp>

namespace tools::tilesmith {

  void render_layer_strip(const EditorState &state, const corundum::tool_host::ThemeColors &theme);

} // namespace tools::tilesmith
