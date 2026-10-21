// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "actions.hpp"
#include "editor_state.hpp"
#include "file_io.hpp"
#include "layout.hpp"
#include "render_graph.hpp"
#include "render_inspector.hpp"
#include "render_item_editor.hpp"
#include "render_node_list.hpp"
#include "render_quest_editor.hpp"
#include "shortcuts.hpp"
#include "validate_quest_refs.hpp"
#include <corundum/gameplay/item/item.hpp>

#include <algorithm>
#include <corundum/core/content_source.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/toolkit/host/tool_config.hpp>
#include <corundum/toolkit/host/tool_host.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <corundum/toolkit/widgets/fonts.hpp>
#include <corundum/toolkit/widgets/ui_theme.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <print>
#include <string>
#include <utility>

using corundum::toolkit::widgets::ApplyEditorThemeRefined;
using corundum::toolkit::widgets::load_theme;
using corundum::toolkit::widgets::load_tool_fonts;
using corundum::toolkit::widgets::ThemeColors;
using tools::loom::action_open;
using tools::loom::action_save;
using tools::loom::action_save_as;
using tools::loom::app_title;
using tools::loom::DocumentKind;
using tools::loom::EditorState;
using tools::loom::k_default_window_h;
using tools::loom::k_default_window_w;
using tools::loom::process_shortcuts;
using tools::loom::ShortcutMap;

namespace {

  void new_dialogue(EditorState &state) {
    state.doc_type_ = DocumentKind::Dialogue;
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

  void new_quest(EditorState &state) {
    state.doc_type_ = DocumentKind::Quest;
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

  void new_item(EditorState &state) {
    state.doc_type_ = DocumentKind::Item;
    state.graph = {};
    state.quest_doc_ = {};
    state.item_doc_.clear();
    state.item_category_ = corundum::gameplay::item::ItemCategory::Misc;
    state.item_doc_.emplace_back();
    state.layout.clear();
    state.file_path.clear();
    state.selected_node = -1;
    state.selected_stage_ = -1;
    state.selected_item_ = 0;
    state.inspector_open = false;
    state.dirty = false;
    state.undo_stack.clear();
  }

} // namespace

// NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity, bugprone-exception-escape)
int main(int argc, char *argv[]) {
  if (argc > 2) {
    std::println(stderr, "Usage: loom [file.json]");
    std::println(stderr, "  Run from the project root directory.");
    return 1;
  }

  auto cfg_result = corundum::toolkit::host::load_tool_config(argc, argv);
  if (!cfg_result) {
    std::println(stderr, "[Loom] FATAL: {}", cfg_result.error());
    return 1;
  }
  const corundum::toolkit::host::ToolConfig cfg = std::move(*cfg_result);

  EditorState state;

  if (!cfg.quests_dir.empty() && std::filesystem::is_directory(cfg.quests_dir)) {
    [[maybe_unused]] const auto quest_count =
        state.quest_registry.load_all(corundum::core::DirectoryContentSource{}, cfg.quests_dir.string());
    state.quests_loaded_ = true;
  }

  if (!cfg.dialogue_dir.empty() && std::filesystem::is_directory(cfg.dialogue_dir)) {
    [[maybe_unused]] const auto graph_count =
        state.graph_registry_.load_all(corundum::core::DirectoryContentSource{}, cfg.dialogue_dir.string());
    state.graphs_loaded_ = true;
  }

  if (argc == 2) {
    auto load_result = load_file(state, argv[1]);
    if (!load_result) {
      std::println(stderr, "[Loom] FATAL: {}", load_result.error());
      return 1;
    }
    if (state.doc_type_ == DocumentKind::Dialogue && state.quests_loaded_) {
      const auto errors = validate_quest_refs(state);
      if (!errors.empty()) {
        state.toast.show(
            std::format("Quest validation: {} warning(s) — check quest references in dialogue actions", errors.size()));
      }
    }
  } else {
    new_dialogue(state);
  }

  const std::string title = app_title(state);

  auto host_result = corundum::toolkit::host::ToolHost::create({
      .width = static_cast<int>(k_default_window_w),
      .height = static_cast<int>(k_default_window_h),
      .title = title,
  });
  if (!host_result) {
    std::println(stderr, "[Loom] FATAL: {}", host_result.error());
    return 1;
  }
  auto host = std::move(*host_result);

  [[maybe_unused]] const auto _ = load_tool_fonts(cfg);

  ThemeColors theme = ApplyEditorThemeRefined();
  if (!cfg.theme_path.empty()) {
    if (auto t = load_theme(cfg.theme_path.string()))
      theme = *t;
    else
      state.toast.show(std::format("[Loom] Theme load failed: {} — using fallback", t.error()));
  }

  bool running = true;

  ShortcutMap shortcuts;
  build_shortcuts(shortcuts, state, *host, running);

  // NOLINTNEXTLINE(readability-function-cognitive-complexity): flat editor UI frame routine.
  host->run([&] {
    if (!running) {
      host->request_close();
      return;
    }

    // ── Root window (fills screen, holds menu + child panels) ─────────────────
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    // NOLINTBEGIN(bugprone-signed-bitwise): ImGui window flags are a signed bitmask.
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);
    // NOLINTEND(bugprone-signed-bitwise)
    ImGui::PopStyleVar(3);

    // ── Menu bar ──────────────────────────────────────────────────────────────
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::BeginMenu("New")) {
          if (ImGui::MenuItem("Dialogue Graph", "Ctrl+N")) {
            new_dialogue(state);
            host->set_title(app_title(state));
          }
          if (ImGui::MenuItem("Quest", "Ctrl+Shift+N")) {
            new_quest(state);
            host->set_title(app_title(state));
          }
          if (ImGui::MenuItem("Item Batch")) {
            new_item(state);
            host->set_title(app_title(state));
          }
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
          action_open(state);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S", false, !state.file_path.empty() || state.dirty)) {
          action_save(state, *host);
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

    if (auto picked = corundum::toolkit::widgets::render_browser(state.popups.save_browser)) {
      state.file_path = *picked;
      const std::string stem = state.file_path.stem().string();
      if (state.doc_type_ == DocumentKind::Quest) {
        state.quest_doc_.quest_id = stem;
      } else if (state.doc_type_ == DocumentKind::Dialogue) {
        state.graph.graph_id = stem;
      }
      auto result = save_file(state);
      if (result) {
        state.dirty = false;
        host->set_title(app_title(state));
      } else {
        state.toast.show(std::format("[Loom] Save error: {}", result.error()));
      }
    }

    if (auto picked = corundum::toolkit::widgets::render_browser(state.popups.open_browser)) {
      auto result = load_file(state, picked->string());
      if (result) {
        host->set_title(app_title(state));
      } else {
        state.toast.show(std::format("[Loom] Load error: {}", result.error()));
      }
    }

    // ── Close confirmation popup ────────────────────────────────────────────
    if (state.popups.show_close_confirm) {
      ImGui::OpenPopup("Unsaved Changes");
      state.popups.show_close_confirm = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("You have unsaved changes. Save before closing?");
      if (ImGui::Button("Save")) {
        auto result = save_file(state);
        if (result) {
          state.dirty = false;
          ImGui::CloseCurrentPopup();
          running = false;
        } else {
          state.toast.show(std::format("[Loom] Save error: {}", result.error()));
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

    // ── Validation error modal ─────────────────────────────────────────────
    if (state.show_validation_modal_) {
      ImGui::OpenPopup("Validation Errors");
      state.show_validation_modal_ = false;
    }
    if (ImGui::BeginPopupModal("Validation Errors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("The graph has the following issues:");
      ImGui::Separator();
      for (const auto &err : state.validation_errors_) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(err.c_str());
      }
      ImGui::Separator();
      if (ImGui::Button("Save Anyway")) {
        auto result = save_file(state);
        if (result) {
          state.dirty = false;
          host->set_title(app_title(state));
          ImGui::CloseCurrentPopup();
        } else {
          state.toast.show(std::format("[Loom] Save error: {}", result.error()));
        }
        state.validation_errors_.clear();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        state.validation_errors_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (state.doc_type_ == DocumentKind::Dialogue) {
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
    } else if (state.doc_type_ == DocumentKind::Quest) {
      // ── QUEST LAYOUT ──
      render_quest_editor(state);
    } else {
      // ── ITEM LAYOUT ──
      render_item_editor(state);
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
      // NOLINTNEXTLINE(bugprone-signed-bitwise): IM_COL32 packs channels with signed shifts.
      ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(180, 40, 40, 220));
      // NOLINTBEGIN(bugprone-signed-bitwise): ImGui window flags are a signed bitmask.
      ImGui::Begin("##toast", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
      // NOLINTEND(bugprone-signed-bitwise)
      ImGui::TextUnformatted(state.toast.message.c_str());
      ImGui::End();
      ImGui::PopStyleColor();
      ImGui::PopStyleVar(2);
    }

    // ── Keyboard shortcuts ───────────────────────────────────────────────
    process_shortcuts(shortcuts);
  });

  return 0;
}
