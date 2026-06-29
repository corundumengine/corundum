#include "render_node_list.hpp"
#include "graph_layout.hpp"
#include "layout.hpp"
#include "node_type_traits.hpp"
#include "render_inspector.hpp"

#include <cstring>
#include <imgui.h>

namespace tools::talesmith {

  namespace {} // namespace

  void render_node_list(EditorState &state) {
    const auto avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##nodes_panel", {state.node_list_width_, avail.y}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);

    if (state.graph.graph_id != std::string_view(state.graph_id_buf_)) {
      std::strncpy(state.graph_id_buf_, state.graph.graph_id.c_str(), sizeof(state.graph_id_buf_) - 1);
      state.graph_id_buf_[sizeof(state.graph_id_buf_) - 1] = '\0';
    }
    if (state.graph.speaker != std::string_view(state.graph_speaker_buf_)) {
      std::strncpy(state.graph_speaker_buf_, state.graph.speaker.c_str(), sizeof(state.graph_speaker_buf_) - 1);
      state.graph_speaker_buf_[sizeof(state.graph_speaker_buf_) - 1] = '\0';
    }

    ImGui::Text("Graph ID:");
    ImGui::SameLine();
    ImGui::InputText("##graph_id", state.graph_id_buf_, sizeof(state.graph_id_buf_));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      state.push_undo_snapshot();
      state.graph.graph_id = std::string(state.graph_id_buf_);
      state.dirty = true;
    }

    ImGui::Text("Speaker:");
    ImGui::SameLine();
    ImGui::InputText("##graph_speaker", state.graph_speaker_buf_, sizeof(state.graph_speaker_buf_));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      state.push_undo_snapshot();
      state.graph.speaker = std::string(state.graph_speaker_buf_);
      state.dirty = true;
    }

    ImGui::Separator();

    if (ImGui::Button("+ Add Node", {state.node_list_width_ - 20.f, 0.f}))
      state.add_node_open = true;

    ImGui::Separator();

    const float avail_h = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("##nodelist", {0.f, avail_h});

    for (int i = 0; i < static_cast<int>(state.graph.nodes.size()); ++i) {
      const auto &node = state.graph.nodes[i];
      const bool is_selected = (i == state.selected_node);

      ImGui::PushID(i);

      std::string label;
      if (node.type == corundum::gameplay::dialogue::NodeType::Talk)
        label = state.graph.speaker.empty() ? node.id : state.graph.speaker + ": " + node.text.substr(0, 30);
      else if (node.type == corundum::gameplay::dialogue::NodeType::Choice)
        label = node.choices.empty() ? node.id : node.choices[0].label.substr(0, 30);
      else
        label = node.id;

      if (label.size() > 35)
        label = label.substr(0, 32) + "...";

      ImGui::PushStyleColor(ImGuiCol_Header, node_list_color(node.type));
      ImGui::PushStyleColor(ImGuiCol_HeaderHovered, node_list_color(node.type));
      ImGui::PushStyleColor(ImGuiCol_HeaderActive, node_list_color(node.type));
      const bool selectable_clicked =
          ImGui::Selectable(node.id.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick, {0.f, 0.f});
      if (selectable_clicked) {
        state.selected_node = i;
        state.inspector_open = true;
      }
      ImGui::PopStyleColor(3);

      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", (label + " [" + node_label(node.type) + "]").c_str());

      ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::EndChild();

    if (state.add_node_open) {
      ImGui::OpenPopup("Add Node");
      state.add_node_open = false;
    }

    if (ImGui::BeginPopupModal("Add Node", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Node ID:");
      ImGui::InputText("##new_id", state.add_node_id_buf, sizeof(state.add_node_id_buf));

      static const char *types[] = {"Talk", "Choice", "Event", "End"};
      ImGui::Combo("Type", &state.add_node_type, types, 4);

      if (ImGui::Button("Create") && state.add_node_id_buf[0] != '\0') {
        state.push_undo_snapshot();
        corundum::gameplay::dialogue::Node new_node;
        new_node.id = state.add_node_id_buf;
        new_node.type = static_cast<corundum::gameplay::dialogue::NodeType>(state.add_node_type);
        state.graph.id_to_index[new_node.id] = state.graph.nodes.size();
        state.graph.nodes.push_back(std::move(new_node));
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
        state.add_node_id_buf[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        state.add_node_id_buf[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (state.selected_node >= 0 && state.selected_node < static_cast<int>(state.graph.nodes.size())) {
      if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete) &&
          ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        state.push_undo_snapshot();
        auto &nodes = state.graph.nodes;
        const auto idx = static_cast<std::size_t>(state.selected_node);
        state.graph.id_to_index.erase(nodes[idx].id);
        nodes.erase(nodes.begin() + idx);
        for (std::size_t j = idx; j < nodes.size(); ++j)
          state.graph.id_to_index[nodes[j].id] = j;
        state.selected_node = -1;
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
      }
    }
  }

} // namespace tools::talesmith
