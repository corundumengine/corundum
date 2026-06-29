#pragma once

#include "editor_state.hpp"
#include "file_io.hpp"
#include "graph_layout.hpp"

#include "../../platform/tool_app.hpp"

#include <flat_map>
#include <format>
#include <functional>
#include <imgui.h>

namespace tools::talesmith {

  using ShortcutAction = std::function<void()>;
  using ShortcutMap = std::flat_map<ImGuiKeyChord, ShortcutAction>;

  inline void build_shortcuts(ShortcutMap &map, EditorState &state, tools::ToolApp &app, bool &running) {
    map.clear();

    map[ImGuiMod_Ctrl | ImGuiKey_N] = [&]() {
      state.graph = {};
      state.graph.graph_id = "untitled";
      state.file_path.clear();
      state.selected_node = -1;
      state.last_scroll_target_ = -1;
      state.inspector_open = false;
      state.dirty = false;
      state.layout.clear();
      state.undo_stack.clear();
      app.set_title("Talesmith :: Untitled");
    };

    map[ImGuiMod_Ctrl | ImGuiKey_O] = [&]() { state.popups.show_open = true; };

    map[ImGuiMod_Ctrl | ImGuiKey_S] = [&]() {
      if (state.file_path.empty()) {
        auto default_name = state.graph.graph_id + ".json";
        std::memcpy(state.popups.save_as_path_buf, default_name.c_str(),
                    std::min(default_name.size(), sizeof(state.popups.save_as_path_buf) - 1));
        state.popups.show_save_as = true;
      } else {
        auto result = save_graph(state);
        if (result) {
          state.dirty = false;
          app.set_title("Talesmith :: " + state.file_path.filename().string());
        } else {
          state.toast.show(std::format("[Talesmith] Save error: {}", result.error()));
        }
      }
    };

    map[ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S] = [&]() {
      auto default_name =
          state.file_path.empty() ? state.graph.graph_id + ".json" : state.file_path.filename().string();
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
        state.graph = std::move(snap.graph);
        state.selected_node = snap.selected_node;
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
      }
    };

    map[ImGuiMod_Ctrl | ImGuiKey_Y] = [&]() {
      GraphSnapshot snap;
      if (state.undo_stack.redo(snap)) {
        state.graph = std::move(snap.graph);
        state.selected_node = snap.selected_node;
        state.dirty = true;
        recompute_layout(state.graph, state.layout, state.graph_width_);
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
