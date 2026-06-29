#include "render_inspector.hpp"
#include "graph_layout.hpp"
#include "node_type_traits.hpp"

#include <algorithm>
#include <cstring>
#include <format>
#include <imgui.h>
#include <vector>

namespace tools::talesmith {

  namespace {

    void copy_to_buf(const std::string &src, char *buf, int sz) {
      std::memset(buf, 0, static_cast<std::size_t>(sz));
      std::memcpy(buf, src.data(), std::min(src.size(), static_cast<std::size_t>(sz - 1)));
    }

    std::string from_buf(const char *buf) {
      return std::string(buf);
    }

    void render_quest_action_quick_add(EditorState &state, std::vector<std::string> &actions) {
      if (!state.quests_loaded_)
        return;

      std::vector<const char *> quest_ids;
      for (const auto &[id, quest] : state.quest_registry)
        quest_ids.push_back(id.c_str());
      if (quest_ids.empty())
        return;

      auto &q = state.inspector_bufs;
      if (q.quick_quest_idx >= static_cast<int>(quest_ids.size()))
        q.quick_quest_idx = 0;

      ImGui::SeparatorText("Quick Add Quest Action");

      if (ImGui::BeginCombo("##qaq_quest", quest_ids[q.quick_quest_idx])) {
        for (int i = 0; i < static_cast<int>(quest_ids.size()); ++i) {
          if (ImGui::Selectable(quest_ids[i], q.quick_quest_idx == i))
            q.quick_quest_idx = i;
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine();
      const char *action_types[] = {"quest_start", "quest_advance"};
      ImGui::Combo("##qaq_action", &q.quick_action_type, action_types, 2);

      if (q.quick_action_type == 1) {
        const auto *quest = state.quest_registry.find(quest_ids[q.quick_quest_idx]);
        if (quest && !quest->stages.empty()) {
          ImGui::SameLine();
          if (q.quick_stage_idx >= static_cast<int>(quest->stages.size()))
            q.quick_stage_idx = 0;
          const char *preview = quest->stages[q.quick_stage_idx].name.c_str();
          if (ImGui::BeginCombo("##qaq_stage", preview)) {
            for (int i = 0; i < static_cast<int>(quest->stages.size()); ++i) {
              if (ImGui::Selectable(quest->stages[i].name.c_str(), q.quick_stage_idx == i))
                q.quick_stage_idx = i;
            }
            ImGui::EndCombo();
          }
        }
      }

      ImGui::SameLine();
      if (ImGui::SmallButton("+##qaq_add")) {
        const auto &quest_id = quest_ids[q.quick_quest_idx];
        std::string action_str;
        if (q.quick_action_type == 0) {
          action_str = std::format("quest_start('{}')", quest_id);
        } else {
          const auto *quest = state.quest_registry.find(quest_id);
          if (quest && q.quick_stage_idx < static_cast<int>(quest->stages.size())) {
            action_str = std::format("quest_advance('{}', '{}')", quest_id, quest->stages[q.quick_stage_idx].name);
          }
        }
        if (!action_str.empty()) {
          state.push_undo_snapshot();
          actions.push_back(action_str);
          state.dirty = true;
        }
      }
    }

    void render_quest_condition_quick_add(EditorState &state, char *cond_buf, int cond_buf_size) {
      if (!state.quests_loaded_)
        return;

      std::vector<const char *> quest_ids;
      for (const auto &[id, quest] : state.quest_registry)
        quest_ids.push_back(id.c_str());
      if (quest_ids.empty())
        return;

      auto &q = state.inspector_bufs;
      if (q.quick_quest_idx >= static_cast<int>(quest_ids.size()))
        q.quick_quest_idx = 0;

      ImGui::SeparatorText("Quick Add Condition");

      if (ImGui::BeginCombo("##qac_quest", quest_ids[q.quick_quest_idx])) {
        for (int i = 0; i < static_cast<int>(quest_ids.size()); ++i) {
          if (ImGui::Selectable(quest_ids[i], q.quick_quest_idx == i))
            q.quick_quest_idx = i;
        }
        ImGui::EndCombo();
      }

      ImGui::SameLine();
      const char *cond_types[] = {"quest_started", "quest_resolved", "quest_failed", "quest_at"};
      ImGui::Combo("##qac_cond", &q.quick_cond_type, cond_types, 4);

      if (q.quick_cond_type == 3) {
        const auto *quest = state.quest_registry.find(quest_ids[q.quick_quest_idx]);
        if (quest && !quest->stages.empty()) {
          ImGui::SameLine();
          if (q.quick_cond_stage_idx >= static_cast<int>(quest->stages.size()))
            q.quick_cond_stage_idx = 0;
          const char *preview = quest->stages[q.quick_cond_stage_idx].name.c_str();
          if (ImGui::BeginCombo("##qac_stage", preview)) {
            for (int i = 0; i < static_cast<int>(quest->stages.size()); ++i) {
              if (ImGui::Selectable(quest->stages[i].name.c_str(), q.quick_cond_stage_idx == i))
                q.quick_cond_stage_idx = i;
            }
            ImGui::EndCombo();
          }
        }
      }

      ImGui::SameLine();
      if (ImGui::SmallButton("+##qac_add")) {
        const auto &quest_id = quest_ids[q.quick_quest_idx];
        std::string cond_str;
        if (q.quick_cond_type == 0)
          cond_str = std::format("quest_started({})", quest_id);
        else if (q.quick_cond_type == 1)
          cond_str = std::format("quest_resolved({})", quest_id);
        else if (q.quick_cond_type == 2)
          cond_str = std::format("quest_failed({})", quest_id);
        else {
          const auto *quest = state.quest_registry.find(quest_id);
          if (quest && q.quick_cond_stage_idx < static_cast<int>(quest->stages.size()))
            cond_str = std::format("quest_at({}, {})", quest_id, quest->stages[q.quick_cond_stage_idx].name);
        }
        if (!cond_str.empty()) {
          std::memset(cond_buf, 0, static_cast<std::size_t>(cond_buf_size));
          std::memcpy(cond_buf, cond_str.data(),
                      std::min(cond_str.size(), static_cast<std::size_t>(cond_buf_size - 1)));
          state.dirty = true;
        }
      }
    }

    void render_talk_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      ImGui::InputTextMultiline("Text", state.inspector_bufs.text_buf, sizeof(state.inspector_bufs.text_buf),
                                {400.f, 80.f});
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        node.text = from_buf(state.inspector_bufs.text_buf);
        state.dirty = true;
      }
      ImGui::InputText("Next Node", state.inspector_bufs.next_id_buf, sizeof(state.inspector_bufs.next_id_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        node.next_id = from_buf(state.inspector_bufs.next_id_buf);
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
      }
      ImGui::Text("Metadata:");
      int meta_id = 0;
      for (auto it = node.metadata.begin(); it != node.metadata.end(); ++meta_id) {
        ImGui::PushID(meta_id);
        ImGui::Text("%s", (it->first + " = " + it->second).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
          state.push_undo_snapshot();
          it = node.metadata.erase(it);
          state.dirty = true;
          ImGui::PopID();
          continue;
        }
        ImGui::PopID();
        ++it;
      }
      ImGui::InputText("##meta_key", state.inspector_bufs.meta_key_buf, sizeof(state.inspector_bufs.meta_key_buf));
      ImGui::SameLine();
      ImGui::InputText("##meta_val", state.inspector_bufs.meta_val_buf, sizeof(state.inspector_bufs.meta_val_buf));
      ImGui::SameLine();
      if (ImGui::SmallButton("+##meta") && state.inspector_bufs.meta_key_buf[0]) {
        state.push_undo_snapshot();
        node.metadata[state.inspector_bufs.meta_key_buf] = state.inspector_bufs.meta_val_buf;
        state.dirty = true;
        std::memset(state.inspector_bufs.meta_key_buf, 0, sizeof(state.inspector_bufs.meta_key_buf));
        std::memset(state.inspector_bufs.meta_val_buf, 0, sizeof(state.inspector_bufs.meta_val_buf));
      }
    }

    void render_choice_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      for (int i = 0; i < static_cast<int>(node.choices.size()); ++i) {
        auto &ch = node.choices[i];
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::Text("Choice %d", i);

        char local_label[256];
        copy_to_buf(ch.label, local_label, sizeof(local_label));
        ImGui::InputText("Label", local_label, sizeof(local_label));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          ch.label = from_buf(local_label);
          state.dirty = true;
        }

        char local_target[128];
        copy_to_buf(ch.target_id, local_target, sizeof(local_target));
        ImGui::InputText("Target", local_target, sizeof(local_target));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          ch.target_id = from_buf(local_target);
          state.dirty = true;
        }

        char local_cond[256];
        if (ch.condition) {
          copy_to_buf(*ch.condition, local_cond, sizeof(local_cond));
        } else {
          local_cond[0] = '\0';
        }
        ImGui::InputText("Condition", local_cond, sizeof(local_cond));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          std::string cond_str = from_buf(local_cond);
          if (cond_str.empty())
            ch.condition.reset();
          else
            ch.condition = std::move(cond_str);
          state.dirty = true;
        }
        if (state.quests_loaded_) {
          std::string cond_before = from_buf(local_cond);
          render_quest_condition_quick_add(state, local_cond, sizeof(local_cond));
          std::string cond_after = from_buf(local_cond);
          if (cond_after != cond_before) {
            ch.condition = std::move(cond_after);
            state.dirty = true;
          }
        }

        static const char *seq_labels[] = {"None", "Once", "Cycle", "Random"};
        int local_seq = static_cast<int>(ch.sequence);
        ImGui::Combo("Sequence", &local_seq, seq_labels, 4);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          ch.sequence = static_cast<corundum::gameplay::dialogue::SequenceMode>(local_seq);
          state.dirty = true;
        }

        int min_visits = ch.min_visits.value_or(0);
        if (ImGui::InputInt("Min Visits", &min_visits)) {
          state.push_undo_snapshot();
          if (min_visits > 0)
            ch.min_visits = min_visits;
          else
            ch.min_visits.reset();
          state.dirty = true;
        }

        ImGui::Text("Actions:");
        for (int j = 0; j < static_cast<int>(ch.actions.size()); ++j) {
          ImGui::PushID(j + 1000);
          ImGui::BulletText("%s", ch.actions[j].c_str());
          ImGui::PopID();
        }
        copy_to_buf("", state.inspector_bufs.action_buf, sizeof(state.inspector_bufs.action_buf));
        ImGui::InputText("##act", state.inspector_bufs.action_buf, sizeof(state.inspector_bufs.action_buf));
        ImGui::SameLine();
        if (ImGui::SmallButton("+##actadd") && state.inspector_bufs.action_buf[0]) {
          state.push_undo_snapshot();
          ch.actions.emplace_back(state.inspector_bufs.action_buf);
          state.dirty = true;
          std::memset(state.inspector_bufs.action_buf, 0, sizeof(state.inspector_bufs.action_buf));
        }

        render_quest_action_quick_add(state, ch.actions);

        ImGui::SameLine();
        if (ImGui::SmallButton("Delete Choice")) {
          state.push_undo_snapshot();
          node.choices.erase(node.choices.begin() + i--);
          state.dirty = true;
        }

        ImGui::PopID();
      }

      if (ImGui::Button("Add Choice")) {
        state.push_undo_snapshot();
        node.choices.emplace_back();
        state.dirty = true;
      }
    }

    void render_event_editor(corundum::gameplay::dialogue::Node &node, EditorState &state) {
      ImGui::InputText("Next Node", state.inspector_bufs.next_id_buf, sizeof(state.inspector_bufs.next_id_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        node.next_id = from_buf(state.inspector_bufs.next_id_buf);
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
      }
      ImGui::Text("Actions:");
      for (int j = 0; j < static_cast<int>(node.actions.size()); ++j) {
        ImGui::PushID(j);
        ImGui::BulletText("%s", node.actions[j].c_str());
        ImGui::PopID();
      }
      copy_to_buf("", state.inspector_bufs.action_buf, sizeof(state.inspector_bufs.action_buf));
      ImGui::InputText("##act", state.inspector_bufs.action_buf, sizeof(state.inspector_bufs.action_buf));
      ImGui::SameLine();
      if (ImGui::SmallButton("+##actadd") && state.inspector_bufs.action_buf[0]) {
        state.push_undo_snapshot();
        node.actions.emplace_back(state.inspector_bufs.action_buf);
        state.dirty = true;
        std::memset(state.inspector_bufs.action_buf, 0, sizeof(state.inspector_bufs.action_buf));
      }
      render_quest_action_quick_add(state, node.actions);
    }

  } // namespace

  void render_inspector(EditorState &state, float width, float height) {
    if (state.selected_node < 0 || state.selected_node >= static_cast<int>(state.graph.nodes.size())) {
      state.inspector_open = false;
      return;
    }

    auto &node = state.graph.nodes[state.selected_node];

    const float avail_y = (height > 0.f) ? height : ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##inspector_panel", {width, avail_y}, ImGuiChildFlags_Borders);

    ImGui::Text("Inspector");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
    if (ImGui::SmallButton("X"))
      state.inspector_open = false;

    ImGui::Separator();

    copy_to_buf(node.id, state.inspector_bufs.id_buf, sizeof(state.inspector_bufs.id_buf));
    ImGui::Text("ID:");
    ImGui::SameLine();
    ImGui::InputText("##node_id", state.inspector_bufs.id_buf, sizeof(state.inspector_bufs.id_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      auto new_id = from_buf(state.inspector_bufs.id_buf);
      if (!new_id.empty() && new_id != node.id) {
        state.push_undo_snapshot();
        state.graph.id_to_index.erase(node.id);
        node.id = new_id;
        state.graph.id_to_index[node.id] = static_cast<std::size_t>(state.selected_node);
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
      }
    }

    ImGui::Text("Type: %s", node_label(node.type));

    ImGui::Separator();

    switch (node.type) {
    case corundum::gameplay::dialogue::NodeType::Talk:
      copy_to_buf(node.text, state.inspector_bufs.text_buf, sizeof(state.inspector_bufs.text_buf));
      copy_to_buf(node.next_id, state.inspector_bufs.next_id_buf, sizeof(state.inspector_bufs.next_id_buf));
      render_talk_editor(node, state);
      break;
    case corundum::gameplay::dialogue::NodeType::Choice:
      render_choice_editor(node, state);
      break;
    case corundum::gameplay::dialogue::NodeType::Event:
      copy_to_buf(node.next_id, state.inspector_bufs.next_id_buf, sizeof(state.inspector_bufs.next_id_buf));
      render_event_editor(node, state);
      break;
    case corundum::gameplay::dialogue::NodeType::End:
      ImGui::Text("No editable properties.");
      break;
    default:
      break;
    }

    ImGui::EndChild();
  }

} // namespace tools::talesmith
