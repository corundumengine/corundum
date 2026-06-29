#include "file_io.hpp"
#include "graph_layout.hpp"

#include <corundum/gameplay/dialogue/loader.hpp>
#include <corundum/gameplay/quest/loader.hpp>

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

    json j;
    j["type"] = "graph";
    j["id"] = state.graph.graph_id;

    if (!state.graph.speaker.empty())
      j["speaker"] = state.graph.speaker;

    if (!state.graph.variables.empty()) {
      json vars = json::object();
      for (const auto &[k, v] : state.graph.variables)
        vars[k] = v;
      j["variables"] = vars;
    }

    json nodes_arr = json::array();
    for (const auto &node : state.graph.nodes) {
      json nj;
      nj["id"] = node.id;

      switch (node.type) {
      case corundum::gameplay::dialogue::NodeType::Talk:
        nj["type"] = "talk";
        nj["text"] = node.text;
        if (!node.next_id.empty())
          nj["next"] = node.next_id;
        break;
      case corundum::gameplay::dialogue::NodeType::Choice:
        nj["type"] = "choice";
        {
          json choices = json::array();
          for (const auto &ch : node.choices) {
            json cj;
            cj["label"] = ch.label;
            cj["target"] = ch.target_id;
            if (ch.condition)
              cj["condition"] = *ch.condition;
            if (!ch.actions.empty())
              cj["actions"] = ch.actions;
            if (ch.sequence != corundum::gameplay::dialogue::SequenceMode::None) {
              if (ch.sequence == corundum::gameplay::dialogue::SequenceMode::Once)
                cj["sequence"] = "once";
              else if (ch.sequence == corundum::gameplay::dialogue::SequenceMode::Cycle)
                cj["sequence"] = "cycle";
              else if (ch.sequence == corundum::gameplay::dialogue::SequenceMode::Random)
                cj["sequence"] = "random";
            }
            if (ch.min_visits)
              cj["min_visits"] = *ch.min_visits;
            choices.push_back(cj);
          }
          nj["choices"] = choices;
        }
        break;
      case corundum::gameplay::dialogue::NodeType::Event:
        nj["type"] = "event";
        if (!node.actions.empty())
          nj["actions"] = node.actions;
        if (!node.next_id.empty())
          nj["next"] = node.next_id;
        break;
      case corundum::gameplay::dialogue::NodeType::End:
        nj["type"] = "end";
        break;
      default:
        break;
      }

      if (!node.metadata.empty()) {
        json meta = json::object();
        for (const auto &[k, v] : node.metadata)
          meta[k] = v;
        nj["metadata"] = meta;
      }

      nodes_arr.push_back(nj);
    }
    j["nodes"] = nodes_arr;

    std::ofstream f(state.file_path);
    if (!f)
      return std::unexpected(std::format("Cannot write to {}", state.file_path.string()));
    f << j.dump(2) << '\n';
    return {};
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

    json j;
    j["type"] = "quest";
    j["id"] = state.quest_doc_.quest_id;
    j["name"] = state.quest_doc_.name;
    j["description"] = state.quest_doc_.description;

    json stages_arr = json::array();
    for (const auto &s : state.quest_doc_.stages) {
      json sj;
      sj["name"] = s.name;
      sj["sequence"] = s.sequence;
      if (s.resolved)
        sj["resolved"] = true;
      if (s.failed)
        sj["failed"] = true;
      if (!s.objectives.empty()) {
        json objs = json::array();
        for (const auto &obj : s.objectives) {
          json oj;
          oj["text"] = obj.text;
          if (obj.done_condition)
            oj["done_condition"] = *obj.done_condition;
          objs.push_back(oj);
        }
        sj["objectives"] = objs;
      }
      stages_arr.push_back(sj);
    }
    j["stages"] = stages_arr;

    std::ofstream f(state.file_path);
    if (!f)
      return std::unexpected(std::format("Cannot write to {}", state.file_path.string()));
    f << j.dump(2) << '\n';
    return {};
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
      f >> j;
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
