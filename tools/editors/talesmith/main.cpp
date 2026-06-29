#include "../../platform/tool_app.hpp"
#include "common/ui_theme.hpp"
#include "editor_state.hpp"
#include "file_io.hpp"
#include "layout.hpp"
#include "render_graph.hpp"
#include "render_inspector.hpp"
#include "render_node_list.hpp"
#include "render_quest_editor.hpp"
#include "shortcuts.hpp"
#include "validate_quest_refs.hpp"

#include <corundum/core/game_config.hpp>
#include <format>
#include <imgui.h>
#include <print>
#include <string>

using tools::talesmith::DocumentType;
using tools::talesmith::EditorState;
using tools::talesmith::k_default_window_h;
using tools::talesmith::k_default_window_w;
using tools::talesmith::process_shortcuts;
using tools::talesmith::ShortcutMap;
using tools::theme::ApplyEditorThemeRefined;
using tools::theme::load_theme;
using tools::theme::ThemeColors;

static void new_dialogue(EditorState &state) {
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
}

static void new_quest(EditorState &state) {
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
}

static std::string app_title(const EditorState &state) {
  std::string t = "Talesmith";
  if (!state.file_path.empty()) {
    t += " :: " + state.file_path.filename().string();
  } else if (state.doc_type_ == DocumentType::Quest) {
    t += " :: Untitled Quest";
  } else {
    t += " :: Untitled Dialogue";
  }
  return t;
}

static void action_save(EditorState &state, tools::ToolApp &app) {
  if (state.file_path.empty()) {
    auto default_name = default_doc_name(state);
    std::memcpy(state.popups.save_as_path_buf, default_name.c_str(),
                std::min(default_name.size(), sizeof(state.popups.save_as_path_buf) - 1));
    state.popups.show_save_as = true;
  } else {
    auto result = save_file(state);
    if (result) {
      state.dirty = false;
      app.set_title(app_title(state));
    } else {
      state.toast.show(std::format("[Talesmith] Save error: {}", result.error()));
    }
  }
}

static void action_save_as(EditorState &state) {
  auto default_name = default_doc_name(state);
  std::memcpy(state.popups.save_as_path_buf, default_name.c_str(),
              std::min(default_name.size(), sizeof(state.popups.save_as_path_buf) - 1));
  state.popups.show_save_as = true;
}

static void action_open(EditorState &state) {
  state.popups.show_open = true;
}

int main(int argc, char *argv[]) {
  if (argc > 2) {
    std::println(stderr, "Usage: talesmith [dialogue.json]");
    std::println(stderr, "  Run from the project root directory.");
    return 1;
  }

  auto cfg_result = corundum::core::load_game_config("tools/tools.json");
  if (!cfg_result) {
    std::println(stderr, "[Talesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::core::GameConfig cfg = std::move(*cfg_result);

  EditorState state;

  if (!cfg.paths.quests_dir.empty() && std::filesystem::is_directory(cfg.paths.quests_dir)) {
    [[maybe_unused]] auto quest_count = state.quest_registry.load_all(cfg.paths.quests_dir);
    state.quests_loaded_ = true;
  }

  if (argc == 2) {
    auto load_result = load_file(state, argv[1]);
    if (!load_result) {
      std::println(stderr, "[Talesmith] FATAL: {}", load_result.error());
      return 1;
    }
    if (state.doc_type_ == DocumentType::Dialogue && state.quests_loaded_) {
      auto errors = validate_quest_refs(state);
      if (!errors.empty()) {
        state.toast.show(
            std::format("Quest validation: {} warning(s) — check quest references in dialogue actions", errors.size()));
      }
    }
  } else {
    new_dialogue(state);
  }

  const std::string title = app_title(state);

  tools::ToolApp app{static_cast<int>(k_default_window_w), static_cast<int>(k_default_window_h), title};

  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  const auto ui_path = std::format("{}/{}", cfg.paths.font_dir, cfg.paths.ui_font);
  io.Fonts->AddFontFromFileTTF(ui_path.c_str(), 18.0f);

  ThemeColors theme;
  if (auto t = load_theme("tools/editors/common/editor_dark.json"); t)
    theme = *t;
  else {
    state.toast.show(std::format("[Talesmith] Theme load failed: {} — using fallback", t.error()));
    theme = ApplyEditorThemeRefined();
  }

  bool running = true;

  ShortcutMap shortcuts;
  build_shortcuts(shortcuts, state, app, running);

  app.run([&]() {
    if (!running) {
      app.close();
      return;
    }

    // ── Root window (fills screen, holds menu + child panels) ─────────────────
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(3);

    // ── Menu bar ──────────────────────────────────────────────────────────────
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu("New")) {
          if (ImGui::MenuItem("Dialogue Graph", "Ctrl+N")) {
            new_dialogue(state);
            app.set_title(app_title(state));
          }
          if (ImGui::MenuItem("Quest", "Ctrl+Shift+N")) {
            new_quest(state);
            app.set_title(app_title(state));
          }
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
          action_open(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !state.file_path.empty() || state.dirty)) {
          action_save(state, app);
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
          action_save_as(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
          if (state.dirty)
            state.popups.show_close_confirm = true;
          else
            running = false;
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    // ── Save As popup ─────────────────────────────────────────────────────────
    if (state.popups.show_save_as) {
      ImGui::OpenPopup("Save Dialogue Graph As");
      state.popups.show_save_as = false;
    }
    if (ImGui::BeginPopupModal("Save Dialogue Graph As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("File path:");
      ImGui::InputText("##savepath", state.popups.save_as_path_buf, sizeof(state.popups.save_as_path_buf));
      if (ImGui::Button("Save") && state.popups.save_as_path_buf[0] != '\0') {
        std::string path(state.popups.save_as_path_buf);
        if (!path.ends_with(".json"))
          path += ".json";
        state.file_path = path;
        if (state.doc_type_ == DocumentType::Quest) {
          state.quest_doc_.quest_id = std::filesystem::path(path).stem().string();
        } else {
          state.graph.graph_id = std::filesystem::path(path).stem().string();
        }
        auto result = save_file(state);
        if (result) {
          state.dirty = false;
          app.set_title(app_title(state));
          ImGui::CloseCurrentPopup();
        } else {
          state.toast.show(std::format("[Talesmith] Save error: {}", result.error()));
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        state.popups.save_as_path_buf[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Open popup ────────────────────────────────────────────────────────────
    if (state.popups.show_open) {
      ImGui::OpenPopup("Open Dialogue Graph");
      state.popups.show_open = false;
    }
    if (ImGui::BeginPopupModal("Open Dialogue Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("File path:");
      ImGui::InputText("##openpath", state.popups.open_path_buf, sizeof(state.popups.open_path_buf));
      if (ImGui::Button("Open") && state.popups.open_path_buf[0] != '\0') {
        std::string path(state.popups.open_path_buf);
        auto result = load_file(state, path);
        if (result) {
          app.set_title(app_title(state));
          std::memset(state.popups.open_path_buf, 0, sizeof(state.popups.open_path_buf));
          ImGui::CloseCurrentPopup();
        } else {
          state.toast.show(std::format("[Talesmith] Load error: {}", result.error()));
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        std::memset(state.popups.open_path_buf, 0, sizeof(state.popups.open_path_buf));
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Close confirmation popup ────────────────────────────────────────────
    if (state.popups.show_close_confirm) {
      ImGui::OpenPopup("Unsaved Changes");
      state.popups.show_close_confirm = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("You have unsaved changes. Save before closing?");
      if (ImGui::Button("Save")) {
        auto result = save_file(state);
        if (result) {
          state.dirty = false;
          ImGui::CloseCurrentPopup();
          running = false;
        } else {
          state.toast.show(std::format("[Talesmith] Save error: {}", result.error()));
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Don't Save")) {
        ImGui::CloseCurrentPopup();
        running = false;
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (state.doc_type_ == DocumentType::Dialogue) {
      // ── DIALOGUE LAYOUT: Node list | Graph | Inspector ──
      const ImVec2 root_avail = ImGui::GetContentRegionAvail();
      state.graph_width_ =
          root_avail.x - state.node_list_width_ - (state.inspector_open ? state.inspector_width_ : 0.f);

      render_node_list(state);

      ImGui::SameLine();
      ImGui::InvisibleButton("##splitter_nl", {4.f, ImGui::GetContentRegionAvail().y});
      if (ImGui::IsItemActive())
        state.node_list_width_ = std::clamp(state.node_list_width_ + ImGui::GetIO().MouseDelta.x, 160.f, 500.f);
      if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

      render_graph(state);

      if (state.inspector_open) {
        ImGui::SameLine();
        ImGui::InvisibleButton("##splitter_right", {4.f, ImGui::GetContentRegionAvail().y});
        if (ImGui::IsItemActive())
          state.inspector_width_ = std::clamp(state.inspector_width_ + ImGui::GetIO().MouseDelta.x, 200.f, 800.f);
        if (ImGui::IsItemHovered())
          ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        ImGui::SameLine();
        render_inspector(state, state.inspector_width_);
      }
    } else {
      // ── QUEST LAYOUT ──
      render_quest_editor(state);
    }

    ImGui::End(); // ##root

    // ── Toast notification bar ───────────────────────────────────────────
    if (state.toast.frames_remaining > 0) {
      state.toast.frames_remaining--;
      const ImGuiViewport *viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 36.f});
      ImGui::SetNextWindowSize({viewport->WorkSize.x, 36.f});
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
      ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(180, 40, 40, 220));
      ImGui::Begin("##toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
      ImGui::Text("%s", state.toast.message.c_str());
      ImGui::End();
      ImGui::PopStyleColor();
      ImGui::PopStyleVar(2);
    }

    // ── Keyboard shortcuts ───────────────────────────────────────────────
    process_shortcuts(shortcuts);
  });

  return 0;
}
