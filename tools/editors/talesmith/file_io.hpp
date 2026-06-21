#pragma once

#include "editor_state.hpp"

#include <expected>
#include <string>

namespace tools::talesmith {

  [[nodiscard]] std::expected<void, std::string> save_graph(const EditorState &state);
  [[nodiscard]] std::expected<void, std::string> load_graph_file(EditorState &state, const std::string &path);

} // namespace tools::talesmith
