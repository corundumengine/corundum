#include "../../platform/tool_app.hpp"
#include "common/ui_theme.hpp"
#include "editor_state.hpp"
#include "file_io.hpp"
#include "layout.hpp"
#include "render_graph.hpp"
#include "render_node_list.hpp"

#include <corundum/core/game_config.hpp>
#include <imgui.h>
#include <print>
#include <string>

using tools::talesmith::EditorState;
using tools::talesmith::WINDOW_H;
using tools::talesmith::WINDOW_W;
using tools::theme::ApplyEditorThemeRefined;
using tools::theme::load_theme;
using tools::theme::ThemeColors;

static void new_graph(EditorState &state) {
  state.graph = {};
  state.graph.graph_id = "untitled";
  state.file_path.clear();
  state.selected_node = -1;
  state.inspector_open = false;
  state.dirty = false;
  state.layout.clear();
}

static bool show_save_as = false;
static char save_as_path_buf[512] = {};
static bool show_open = false;
static char open_path_buf[512] = {};

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

  if (argc == 2) {
    auto load_result = load_graph_file(state, argv[1]);
    if (!load_result) {
      std::println(stderr, "[Talesmith] FATAL: {}", load_result.error());
      return 1;
    }
  } else {
    new_graph(state);
  }

  const std::string title =
      (argc == 2) ? std::string("Talesmith :: ") + state.file_path.filename().string() : "Talesmith :: Untitled";

  tools::ToolApp app{WINDOW_W, WINDOW_H, title};

  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  const auto ui_path = std::format("{}/{}", cfg.paths.font_dir, cfg.paths.ui_font);
  io.Fonts->AddFontFromFileTTF(ui_path.c_str(), 18.0f);

  ThemeColors theme;
  if (auto t = load_theme("tools/editors/common/editor_dark.json"); t)
    theme = *t;
  else {
    std::println(stderr, "[Talesmith] Theme load failed: {} — using fallback", t.error());
    theme = ApplyEditorThemeRefined();
  }

  bool running = true;

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
        if (ImGui::MenuItem("New", "Ctrl+N")) {
          new_graph(state);
          app.set_title("Talesmith :: Untitled");
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
          show_open = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !state.file_path.empty() || state.dirty)) {
          if (state.file_path.empty()) {
            std::memcpy(save_as_path_buf, state.graph.graph_id.c_str(), state.graph.graph_id.size() + 1);
            show_save_as = true;
          } else {
            auto result = save_graph(state);
            if (result) {
              state.dirty = false;
              app.set_title("Talesmith :: " + state.file_path.filename().string());
            } else {
              std::println(stderr, "[Talesmith] Save error: {}", result.error());
            }
          }
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
          const auto default_name =
              state.file_path.empty() ? state.graph.graph_id + ".json" : state.file_path.filename().string();
          std::memcpy(save_as_path_buf, default_name.c_str(),
                      std::min(default_name.size(), sizeof(save_as_path_buf) - 1));
          show_save_as = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
          running = false;
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    // ── Save As popup ─────────────────────────────────────────────────────────
    if (show_save_as) {
      ImGui::OpenPopup("Save Dialogue Graph As");
      show_save_as = false;
    }
    if (ImGui::BeginPopupModal("Save Dialogue Graph As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("File path:");
      ImGui::InputText("##savepath", save_as_path_buf, sizeof(save_as_path_buf));
      if (ImGui::Button("Save") && save_as_path_buf[0] != '\0') {
        std::string path(save_as_path_buf);
        if (!path.ends_with(".json"))
          path += ".json";
        state.file_path = path;
        state.graph.graph_id = std::filesystem::path(path).stem().string();
        auto result = save_graph(state);
        if (result) {
          state.dirty = false;
          app.set_title("Talesmith :: " + state.file_path.filename().string());
          ImGui::CloseCurrentPopup();
        } else {
          std::println(stderr, "[Talesmith] Save error: {}", result.error());
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        save_as_path_buf[0] = '\0';
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Open popup ────────────────────────────────────────────────────────────
    if (show_open) {
      ImGui::OpenPopup("Open Dialogue Graph");
      show_open = false;
    }
    if (ImGui::BeginPopupModal("Open Dialogue Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("File path:");
      ImGui::InputText("##openpath", open_path_buf, sizeof(open_path_buf));
      if (ImGui::Button("Open") && open_path_buf[0] != '\0') {
        std::string path(open_path_buf);
        auto result = load_graph_file(state, path);
        if (result) {
          app.set_title("Talesmith :: " + std::filesystem::path(path).filename().string());
          std::memset(open_path_buf, 0, sizeof(open_path_buf));
          ImGui::CloseCurrentPopup();
        } else {
          std::println(stderr, "[Talesmith] Load error: {}", result.error());
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        std::memset(open_path_buf, 0, sizeof(open_path_buf));
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Main panels (children of root, below menu bar) ────────────────────────
    render_node_list(state);
    render_graph(state);

    ImGui::End(); // ##root

    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    const bool ctrl_s = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
    const bool ctrl_shift_s = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S);
    const bool ctrl_o = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O);

    if (ctrl_s || ctrl_shift_s) {
      const auto default_name =
          state.file_path.empty() ? state.graph.graph_id + ".json" : state.file_path.filename().string();
      std::memcpy(save_as_path_buf, default_name.c_str(), std::min(default_name.size(), sizeof(save_as_path_buf) - 1));
      show_save_as = true;
    }
    if (ctrl_o) {
      show_open = true;
    }
  });

  return 0;
}
