#pragma once

#include "editor_state.hpp"
#include "file_io.hpp"
#include "graph_layout.hpp"

#include <corundum/tool_host/tool_host.hpp>

#include <flat_map>
#include <format>
#include <functional>
#include <imgui.h>

namespace tools::talesmith {

  using ShortcutAction = std::function<void()>;
  using ShortcutMap = std::flat_map<ImGuiKeyChord, ShortcutAction>;

  inline void build_shortcuts(ShortcutMap &map, EditorState &state, corundum::tool_host::ToolHost &host,
                              bool &running) {
    map.clear();

    auto apply_undo = [&](const GraphSnapshot &snap) {
      state.graph = snap.graph;
      state.quest_doc_ = snap.quest;
      state.doc_type_ = snap.doc_type;
      state.selected_node = snap.selected_node;
      state.selected_stage_ = snap.selected_stage;
      state.dirty = true;
      if (snap.doc_type == DocumentType::Dialogue)
        recompute_layout(state.graph, state.layout, state.graph_width_);
    };

    map[ImGuiMod_Ctrl | ImGuiKey_N] = [&]() {
      state.doc_type_ = DocumentType::Dialogue;
      state.graph = {};
      state.graph.graph_id = "untitled_dialogue";
      state.quest_doc_ = {};
      state.layout.clear();
      state.file_path.clear();
      state.selected_node = -1;
      state.selected_stage_ = -1;
      state.last_scroll_target_ = -1;
      state.inspector_open = false;
      state.dirty = false;
      state.undo_stack.clear();
      host.set_title("Talesmith :: Untitled Dialogue");
    };

    map[ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N] = [&]() {
      state.doc_type_ = DocumentType::Quest;
      state.graph = {};
      state.quest_doc_ = {};
      state.quest_doc_.quest_id = "untitled_quest";
      state.layout.clear();
      state.file_path.clear();
      state.selected_node = -1;
      state.selected_stage_ = -1;
      state.inspector_open = false;
      state.dirty = false;
      state.undo_stack.clear();
      host.set_title("Talesmith :: Untitled Quest");
    };

    map[ImGuiMod_Ctrl | ImGuiKey_O] = [&]() { state.popups.show_open = true; };

    map[ImGuiMod_Ctrl | ImGuiKey_S] = [&]() {
      if (state.file_path.empty()) {
        auto default_name = default_doc_name(state);
        std::memcpy(state.popups.save_as_path_buf, default_name.c_str(),
                    std::min(default_name.size(), sizeof(state.popups.save_as_path_buf) - 1));
        state.popups.show_save_as = true;
      } else {
        auto result = save_file(state);
        if (result) {
          state.dirty = false;
          host.set_title("Talesmith :: " + state.file_path.filename().string());
        } else {
          state.toast.show(std::format("[Talesmith] Save error: {}", result.error()));
        }
      }
    };

    map[ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S] = [&]() {
      auto default_name = default_doc_name(state);
      std::memcpy(state.popups.save_as_path_buf, default_name.c_str(),
                  std::min(default_name.size(), sizeof(state.popups.save_as_path_buf) - 1));
      state.popups.show_save_as = true;
    };

    map[ImGuiMod_Ctrl | ImGuiKey_Q] = [&]() {
      if (state.dirty) {
        state.popups.show_close_confirm = true;
      } else {
        running = false;
      }
    };

    map[ImGuiMod_Ctrl | ImGuiKey_Z] = [&]() {
      GraphSnapshot snap;
      if (state.undo_stack.undo(snap)) {
        apply_undo(snap);
      }
    };

    map[ImGuiMod_Ctrl | ImGuiKey_Y] = [&]() {
      GraphSnapshot snap;
      if (state.undo_stack.redo(snap)) {
        apply_undo(snap);
      }
    };
  }

  inline void process_shortcuts(const ShortcutMap &map) {
    for (const auto &[chord, action] : map) {
      if (ImGui::IsKeyChordPressed(chord)) {
        action();
        break;
      }
    }
  }

} // namespace tools::talesmith
