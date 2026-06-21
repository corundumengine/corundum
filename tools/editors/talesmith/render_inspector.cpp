#include "render_inspector.hpp"
#include "graph_layout.hpp"

#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace tools::talesmith {

  namespace {

    char id_buf[128];
    char speaker_buf[256];
    char text_buf[1024];
    char next_id_buf[128];
    char cond_buf[256];
    char action_buf[256];
    char label_buf[256];
    char target_buf[128];
    char meta_key_buf[128];
    char meta_val_buf[256];

    int sequence_selection = 0;

    void copy_to_buf(const std::string &src, char *buf, int sz) {
      std::memset(buf, 0, static_cast<std::size_t>(sz));
      std::memcpy(buf, src.data(), std::min(src.size(), static_cast<std::size_t>(sz - 1)));
    }

    std::string from_buf(const char *buf) {
      return std::string(buf);
    }

    void render_talk_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      ImGui::InputText("Speaker", speaker_buf, sizeof(speaker_buf));
      ImGui::InputTextMultiline("Text", text_buf, sizeof(text_buf), {400.f, 80.f});
      ImGui::InputText("Next Node", next_id_buf, sizeof(next_id_buf));
      ImGui::Text("Metadata:");
      int meta_id = 0;
      for (auto it = node.metadata.begin(); it != node.metadata.end(); ++meta_id) {
        ImGui::PushID(meta_id);
        ImGui::Text("%s = %s", it->first.c_str(), it->second.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
          it = node.metadata.erase(it);
          state.dirty = true;
          ImGui::PopID();
          continue;
        }
        ImGui::PopID();
        ++it;
      }
      ImGui::InputText("##meta_key", meta_key_buf, sizeof(meta_key_buf));
      ImGui::SameLine();
      ImGui::InputText("##meta_val", meta_val_buf, sizeof(meta_val_buf));
      ImGui::SameLine();
      if (ImGui::SmallButton("+##meta") && meta_key_buf[0]) {
        node.metadata[meta_key_buf] = meta_val_buf;
        state.dirty = true;
        std::memset(meta_key_buf, 0, sizeof(meta_key_buf));
        std::memset(meta_val_buf, 0, sizeof(meta_val_buf));
      }
    }

    void render_choice_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      for (int i = 0; i < static_cast<int>(node.choices.size()); ++i) {
        auto &ch = node.choices[i];
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::Text("Choice %d", i);
        copy_to_buf(ch.label, label_buf, sizeof(label_buf));
        ImGui::InputText("Label", label_buf, sizeof(label_buf));
        copy_to_buf(ch.target_id, target_buf, sizeof(target_buf));
        ImGui::InputText("Target", target_buf, sizeof(target_buf));

        if (ch.condition) {
          copy_to_buf(*ch.condition, cond_buf, sizeof(cond_buf));
          ImGui::InputText("Condition", cond_buf, sizeof(cond_buf));
        } else {
          cond_buf[0] = '\0';
          ImGui::InputText("Condition", cond_buf, sizeof(cond_buf));
        }

        static const char *seq_labels[] = {"None", "Once", "Cycle", "Random"};
        sequence_selection = static_cast<int>(ch.sequence);
        ImGui::Combo("Sequence", &sequence_selection, seq_labels, 4);
        ch.sequence = static_cast<corundum::gameplay::dialogue::SequenceMode>(sequence_selection);

        int min_visits = ch.min_visits.value_or(0);
        if (ImGui::InputInt("Min Visits", &min_visits)) {
          if (min_visits > 0)
            ch.min_visits = min_visits;
          else
            ch.min_visits.reset();
        }

        ImGui::Text("Actions:");
        for (int j = 0; j < static_cast<int>(ch.actions.size()); ++j) {
          ImGui::PushID(j + 1000);
          ImGui::BulletText("%s", ch.actions[j].c_str());
          ImGui::PopID();
        }
        copy_to_buf("", action_buf, sizeof(action_buf));
        ImGui::InputText("##act", action_buf, sizeof(action_buf));
        ImGui::SameLine();
        if (ImGui::SmallButton("+##actadd") && action_buf[0]) {
          ch.actions.emplace_back(action_buf);
          state.dirty = true;
          std::memset(action_buf, 0, sizeof(action_buf));
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("Delete Choice"))
          node.choices.erase(node.choices.begin() + i--);

        ImGui::PopID();
      }

      if (ImGui::Button("Add Choice")) {
        node.choices.emplace_back();
        state.dirty = true;
      }
    }

    void render_event_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      ImGui::InputText("Next Node", next_id_buf, sizeof(next_id_buf));
      ImGui::Text("Actions:");
      for (int j = 0; j < static_cast<int>(node.actions.size()); ++j) {
        ImGui::PushID(j);
        ImGui::BulletText("%s", node.actions[j].c_str());
        ImGui::PopID();
      }
      copy_to_buf("", action_buf, sizeof(action_buf));
      ImGui::InputText("##act", action_buf, sizeof(action_buf));
      ImGui::SameLine();
      if (ImGui::SmallButton("+##actadd") && action_buf[0]) {
        node.actions.emplace_back(action_buf);
        state.dirty = true;
        std::memset(action_buf, 0, sizeof(action_buf));
      }
    }

    void save_inspector_changes(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      auto old_id = node.id;

      const auto new_id = from_buf(id_buf);
      if (!new_id.empty() && new_id != old_id) {
        state.graph.id_to_index.erase(old_id);
        node.id = new_id;
        state.graph.id_to_index[node.id] = static_cast<std::size_t>(state.selected_node);
        state.dirty = true;
      }

      if (node.type == corundum::gameplay::dialogue::NodeType::Talk) {
        const auto new_speaker = from_buf(speaker_buf);
        const auto new_text = from_buf(text_buf);
        const auto new_next = from_buf(next_id_buf);
        if (node.speaker != new_speaker) {
          node.speaker = new_speaker;
          state.dirty = true;
        }
        if (node.text != new_text) {
          node.text = new_text;
          state.dirty = true;
        }
        if (node.next_id != new_next) {
          node.next_id = new_next;
          state.dirty = true;
        }
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Choice) {
        for (int i = 0; i < static_cast<int>(node.choices.size()); ++i) {
          ImGui::PushID(i);
          // Changes applied live in render_choice_editor
          ImGui::PopID();
        }
      } else if (node.type == corundum::gameplay::dialogue::NodeType::Event) {
        const auto new_next = from_buf(next_id_buf);
        if (node.next_id != new_next) {
          node.next_id = new_next;
          state.dirty = true;
        }
      }

      recompute_layout(state.graph, state.layout);
    }

  } // namespace

  void render_inspector(EditorState &state) {
    if (state.selected_node < 0 || state.selected_node >= static_cast<int>(state.graph.nodes.size())) {
      state.inspector_open = false;
      return;
    }

    auto &node = state.graph.nodes[state.selected_node];

    ImGui::OpenPopup("Node Inspector");

    if (ImGui::BeginPopupModal("Node Inspector", &state.inspector_open, ImGuiWindowFlags_AlwaysAutoResize)) {
      copy_to_buf(node.id, id_buf, sizeof(id_buf));
      ImGui::Text("ID:");
      ImGui::SameLine();
      ImGui::InputText("##node_id", id_buf, sizeof(id_buf));

      const char *type_label = [&]() -> const char * {
        switch (node.type) {
        case corundum::gameplay::dialogue::NodeType::Talk:
          return "Talk";
        case corundum::gameplay::dialogue::NodeType::Choice:
          return "Choice";
        case corundum::gameplay::dialogue::NodeType::Event:
          return "Event";
        case corundum::gameplay::dialogue::NodeType::End:
          return "End";
        default:
          return "???";
        }
      }();
      ImGui::Text("Type: %s", type_label);

      ImGui::Separator();

      switch (node.type) {
      case corundum::gameplay::dialogue::NodeType::Talk:
        copy_to_buf(node.speaker, speaker_buf, sizeof(speaker_buf));
        copy_to_buf(node.text, text_buf, sizeof(text_buf));
        copy_to_buf(node.next_id, next_id_buf, sizeof(next_id_buf));
        render_talk_editor(node, state);
        break;
      case corundum::gameplay::dialogue::NodeType::Choice:
        render_choice_editor(node, state);
        break;
      case corundum::gameplay::dialogue::NodeType::Event:
        copy_to_buf(node.next_id, next_id_buf, sizeof(next_id_buf));
        render_event_editor(node, state);
        break;
      case corundum::gameplay::dialogue::NodeType::End:
        ImGui::Text("No editable properties.");
        break;
      default:
        break;
      }

      ImGui::Separator();

      if (ImGui::Button("Save")) {
        save_inspector_changes(node, state);
        state.inspector_open = false;
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        state.inspector_open = false;

      ImGui::EndPopup();
    }
  }

} // namespace tools::talesmith
