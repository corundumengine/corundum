#include "render_quest_editor.hpp"
#include "graph_layout.hpp"
#include "validate_quest_refs.hpp"

#include <format>
#include <imgui.h>

namespace tools::talesmith {

  void render_quest_editor(EditorState &state) {
    const auto avail = ImGui::GetContentRegionAvail();

    // ── Left panel: Stage list ─────────────────────────────────────────
    ImGui::BeginChild("##quest_stage_list", {state.node_list_width_, avail.y}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::Text("Quest Stages");
    ImGui::Separator();

    const auto avail_h = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("##stage_items", {0.f, avail_h - 40.f});

    for (int i = 0; i < static_cast<int>(state.quest_doc_.stages.size()); ++i) {
      const auto &s = state.quest_doc_.stages[i];
      const bool is_selected = (i == state.selected_stage_);

      const auto label = std::to_string(s.sequence) + ": " + s.name;
      if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowDoubleClick)) {
        state.selected_stage_ = i;
      }
      if (s.resolved) {
        ImGui::SameLine();
        ImGui::TextColored({0.5f, 1.f, 0.5f, 1.f}, "[%s]", s.failed ? "FAILED" : "DONE");
      }
    }

    ImGui::EndChild(); // stage_items

    // Buttons below stage list
    if (ImGui::Button("+ Stage", {state.node_list_width_ - 20.f, 0.f})) {
      state.push_undo_snapshot();
      state.quest_doc_.stages.push_back({});
      state.quest_doc_.stages.back().name = "new_stage";
      state.quest_doc_.stages.back().sequence = static_cast<int>(state.quest_doc_.stages.size());
      state.selected_stage_ = static_cast<int>(state.quest_doc_.stages.size()) - 1;
      state.dirty = true;
    }

    if (state.selected_stage_ >= 0 && state.selected_stage_ < static_cast<int>(state.quest_doc_.stages.size())) {
      if (ImGui::Button("Delete Stage", {state.node_list_width_ - 20.f, 0.f})) {
        state.push_undo_snapshot();
        state.quest_doc_.stages.erase(state.quest_doc_.stages.begin() + state.selected_stage_);
        state.selected_stage_ = -1;
        state.dirty = true;
      }
      if (ImGui::Button("Move Up", {state.node_list_width_ - 20.f, 0.f}) && state.selected_stage_ > 0) {
        state.push_undo_snapshot();
        std::swap(state.quest_doc_.stages[state.selected_stage_], state.quest_doc_.stages[state.selected_stage_ - 1]);
        state.selected_stage_--;
        state.dirty = true;
      }
      if (ImGui::Button("Move Down", {state.node_list_width_ - 20.f, 0.f}) &&
          state.selected_stage_ < static_cast<int>(state.quest_doc_.stages.size()) - 1) {
        state.push_undo_snapshot();
        std::swap(state.quest_doc_.stages[state.selected_stage_], state.quest_doc_.stages[state.selected_stage_ + 1]);
        state.selected_stage_++;
        state.dirty = true;
      }
    }

    ImGui::EndChild(); // quest_stage_list

    // ── Splitter ───────────────────────────────────────────────────────
    ImGui::SameLine();
    ImGui::InvisibleButton("##splitter_quest", {4.f, ImGui::GetContentRegionAvail().y});
    if (ImGui::IsItemHovered())
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // ── Center panel: Stage editor ─────────────────────────────────────
    ImGui::SameLine();
    const float editor_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("##quest_editor", {editor_w, avail.y});

    // Quest metadata
    ImGui::Text("Quest Editor");
    ImGui::Separator();

    char id_buf[256];
    std::memset(id_buf, 0, sizeof(id_buf));
    std::memcpy(id_buf, state.quest_doc_.quest_id.c_str(),
                std::min(state.quest_doc_.quest_id.size(), sizeof(id_buf) - 1));
    ImGui::InputText("Quest ID", id_buf, sizeof(id_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      state.push_undo_snapshot();
      state.quest_doc_.quest_id = std::string(id_buf);
      state.dirty = true;
    }

    char name_buf[256];
    std::memset(name_buf, 0, sizeof(name_buf));
    std::memcpy(name_buf, state.quest_doc_.name.c_str(), std::min(state.quest_doc_.name.size(), sizeof(name_buf) - 1));
    ImGui::InputText("Name", name_buf, sizeof(name_buf));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      state.push_undo_snapshot();
      state.quest_doc_.name = std::string(name_buf);
      state.dirty = true;
    }

    char desc_buf[1024];
    std::memset(desc_buf, 0, sizeof(desc_buf));
    std::memcpy(desc_buf, state.quest_doc_.description.c_str(),
                std::min(state.quest_doc_.description.size(), sizeof(desc_buf) - 1));
    ImGui::InputTextMultiline("Description", desc_buf, sizeof(desc_buf), {editor_w - 20.f, 60.f});
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      state.push_undo_snapshot();
      state.quest_doc_.description = std::string(desc_buf);
      state.dirty = true;
    }

    ImGui::Separator();

    // Selected stage editor
    if (state.selected_stage_ >= 0 && state.selected_stage_ < static_cast<int>(state.quest_doc_.stages.size())) {
      auto &s = state.quest_doc_.stages[state.selected_stage_];

      ImGui::Text("Stage: %d", state.selected_stage_);
      ImGui::Separator();

      char stage_name_buf[256];
      std::memset(stage_name_buf, 0, sizeof(stage_name_buf));
      std::memcpy(stage_name_buf, s.name.c_str(), std::min(s.name.size(), sizeof(stage_name_buf) - 1));
      ImGui::InputText("Stage Name", stage_name_buf, sizeof(stage_name_buf));
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        state.push_undo_snapshot();
        s.name = std::string(stage_name_buf);
        state.dirty = true;
      }

      int seq = s.sequence;
      if (ImGui::InputInt("Sequence", &seq)) {
        state.push_undo_snapshot();
        s.sequence = seq;
        state.dirty = true;
      }

      bool resolved = s.resolved;
      if (ImGui::Checkbox("Resolved", &resolved)) {
        state.push_undo_snapshot();
        s.resolved = resolved;
        state.dirty = true;
      }

      bool failed = s.failed;
      if (ImGui::Checkbox("Failed", &failed)) {
        state.push_undo_snapshot();
        s.failed = failed;
        if (failed)
          s.resolved = true;
        state.dirty = true;
      }

      ImGui::SeparatorText("Objectives");

      int obj_to_delete = -1;
      for (int oi = 0; oi < static_cast<int>(s.objectives.size()); ++oi) {
        auto &obj = s.objectives[oi];
        ImGui::PushID(oi);

        char obj_text_buf[512];
        std::memset(obj_text_buf, 0, sizeof(obj_text_buf));
        std::memcpy(obj_text_buf, obj.text.c_str(), std::min(obj.text.size(), sizeof(obj_text_buf) - 1));
        ImGui::InputText("Text", obj_text_buf, sizeof(obj_text_buf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          obj.text = std::string(obj_text_buf);
          state.dirty = true;
        }

        char obj_cond_buf[256];
        std::memset(obj_cond_buf, 0, sizeof(obj_cond_buf));
        if (obj.done_condition)
          std::memcpy(obj_cond_buf, obj.done_condition->c_str(),
                      std::min(obj.done_condition->size(), sizeof(obj_cond_buf) - 1));
        ImGui::InputText("Done Condition", obj_cond_buf, sizeof(obj_cond_buf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          state.push_undo_snapshot();
          std::string cond_str = std::string(obj_cond_buf);
          if (cond_str.empty())
            obj.done_condition.reset();
          else
            obj.done_condition = std::move(cond_str);
          state.dirty = true;
        }

        if (ImGui::SmallButton("X##obj")) {
          obj_to_delete = oi;
          state.push_undo_snapshot();
        }

        ImGui::PopID();
      }

      if (obj_to_delete >= 0)
        s.objectives.erase(s.objectives.begin() + obj_to_delete);

      if (ImGui::Button("+ Add Objective")) {
        state.push_undo_snapshot();
        s.objectives.push_back({});
        state.dirty = true;
      }
    } else {
      ImGui::TextDisabled("Select a stage from the list to edit its properties.");
    }

    // ── Validation section at bottom of editor ──
    ImGui::Separator();
    // Simple quest validation
    if (state.quest_doc_.quest_id.empty())
      ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  Quest ID is empty");
    if (state.quest_doc_.name.empty())
      ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  Quest name is empty");

    bool has_resolved = false;
    bool names_unique = true;
    bool seqs_unique = true;
    for (std::size_t i = 0; i < state.quest_doc_.stages.size(); ++i) {
      if (state.quest_doc_.stages[i].resolved)
        has_resolved = true;
      for (std::size_t j = i + 1; j < state.quest_doc_.stages.size(); ++j) {
        if (state.quest_doc_.stages[i].name == state.quest_doc_.stages[j].name)
          names_unique = false;
        if (state.quest_doc_.stages[i].sequence == state.quest_doc_.stages[j].sequence)
          seqs_unique = false;
      }
    }
    if (!has_resolved)
      ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  At least one stage must be Resolved");
    if (!names_unique)
      ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  Stage names must be unique");
    if (!seqs_unique)
      ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "  Stage sequences must be unique");

    ImGui::EndChild(); // quest_editor
  }

} // namespace tools::talesmith
