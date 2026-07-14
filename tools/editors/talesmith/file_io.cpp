#include "file_io.hpp"
#include "graph_layout.hpp"

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/dialogue/loader.hpp>
#include <corundum/gameplay/dialogue/serialize.hpp>
#include <corundum/gameplay/quest/loader.hpp>
#include <corundum/gameplay/quest/serialize.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools::talesmith {

  using json = nlohmann::json;

  std::string default_doc_name(const EditorState &state) {
    if (!state.file_path.empty())
      return state.file_path.filename().string();
    if (state.doc_type_ == DocumentType::Quest)
      return state.quest_doc_.quest_id + ".json";
    return state.graph.graph_id + ".json";
  }

  // ── Dialogue save/load ─────────────────────────────────────────────────────

  std::expected<void, std::string> save_graph(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");
    return corundum::core::write_json(state.file_path, corundum::gameplay::dialogue::serialize_graph(state.graph));
  }

  std::expected<void, std::string> load_graph_file(EditorState &state, const std::string &path) {
    auto result = corundum::gameplay::dialogue::load_graph(path);
    if (!result)
      return std::unexpected(result.error());

    state.doc_type_ = DocumentType::Dialogue;
    state.graph = std::move(*result);
    state.quest_doc_ = {};
    state.file_path = path;
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.inspector_open = false;
    state.dirty = false;
    state.last_scroll_target_ = -1;
    state.undo_stack.clear();
    recompute_layout(state.graph, state.layout, state.graph_width_);
    return {};
  }

  // ── Quest save/load ────────────────────────────────────────────────────────

  std::expected<void, std::string> save_quest_file(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");
    return corundum::core::write_json(state.file_path, corundum::gameplay::quest::serialize_quest(state.quest_doc_));
  }

  std::expected<void, std::string> load_quest_file(EditorState &state, const std::string &path) {
    auto result = corundum::gameplay::quest::load_quest(path);
    if (!result)
      return std::unexpected(result.error());

    state.doc_type_ = DocumentType::Quest;
    state.quest_doc_ = std::move(*result);
    state.graph = {};
    state.file_path = path;
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.inspector_open = false;
    state.dirty = false;
    state.undo_stack.clear();
    return {};
  }

  // ── Generic dispatch ───────────────────────────────────────────────────────

  std::expected<void, std::string> load_file(EditorState &state, const std::string &path) {
    // Open and peek at top-level "type" to determine file type
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open {}", path));
    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::parse_error &e) {
      return std::unexpected(std::format("JSON parse error in {}: {}", path, e.what()));
    }
    f.close();

    if (j.contains("type") && j["type"] == "quest")
      return load_quest_file(state, path);
    return load_graph_file(state, path);
  }

  std::expected<void, std::string> save_file(const EditorState &state) {
    if (state.doc_type_ == DocumentType::Quest)
      return save_quest_file(state);
    return save_graph(state);
  }

} // namespace tools::talesmith
