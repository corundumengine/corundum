#pragma once

#include "editor_state.hpp"

#include <expected>
#include <string>

namespace tools::talesmith {

  [[nodiscard]] std::expected<void, std::string> save_graph(const EditorState &state);
  [[nodiscard]] std::expected<void, std::string> load_graph_file(EditorState &state, const std::string &path);

  [[nodiscard]] std::expected<void, std::string> save_quest_file(const EditorState &state);
  [[nodiscard]] std::expected<void, std::string> load_quest_file(EditorState &state, const std::string &path);

  [[nodiscard]] std::expected<void, std::string> load_file(EditorState &state, const std::string &path);
  [[nodiscard]] std::expected<void, std::string> save_file(const EditorState &state);

  [[nodiscard]] std::string default_doc_name(const EditorState &state);

} // namespace tools::talesmith
