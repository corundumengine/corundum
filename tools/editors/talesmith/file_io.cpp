#include "file_io.hpp"
#include "graph_layout.hpp"

#include <corundum/gameplay/dialogue/loader.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools::talesmith {

  using json = nlohmann::json;

  std::expected<void, std::string> save_graph(const EditorState &state) {
    if (state.file_path.empty())
      return std::unexpected("No file path set. Use Save As.");

    json j;
    j["id"] = state.graph.graph_id;

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
        nj["speaker"] = node.speaker;
        nj["text"] = node.text;
        if (!node.next_id.empty() && node.next_id != "end")
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
        if (!node.next_id.empty() && node.next_id != "end")
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

    state.graph = std::move(*result);
    state.file_path = path;
    state.selected_node = -1;
    state.inspector_open = false;
    state.dirty = false;
    recompute_layout(state.graph, state.layout);
    return {};
  }

} // namespace tools::talesmith
