// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "editor_state.hpp"
#include "input.hpp"
#include "layout.hpp"
#include "load.hpp"
#include "render_canvas.hpp"
#include "render_preview_panel.hpp"
#include "render_side_panel.hpp"
#include "render_status_bar.hpp"
#include "save.hpp"
#include <algorithm>
#include <corundum/platform/texture_cache.hpp>
#include <corundum/toolkit/host/tool_config.hpp>
#include <corundum/toolkit/host/tool_host.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <corundum/toolkit/widgets/fonts.hpp>
#include <corundum/toolkit/widgets/ui_theme.hpp>
#include <cstdio>
#include <exception>
#include <imgui.h>
#include <print>
#include <string>
#include <utility>

using corundum::toolkit::ApplyEditorThemeRefined;
using corundum::toolkit::load_theme;
using corundum::toolkit::ThemeColors;
using tools::spritesmith::CanvasContext;
using tools::spritesmith::EditorState;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void try_load_texture(corundum::toolkit::ToolHost &host, EditorState &state,
                             corundum::platform::TextureInfo &texture, std::string &loaded_path) {
  if (state.image_path.empty())
    return;
  if (!loaded_path.empty())
    host.textures().destroy(texture.id);
  auto result = host.textures().load(state.image_path);
  if (!result) {
    std::println(stderr, "[Spritesmith] Cannot load image: {}", result.error());
    loaded_path.clear();
    return;
  }
  texture = *result;
  if (texture.width == 0) {
    std::println(stderr, "[Spritesmith] Cannot load image: {}", state.image_path);
    loaded_path.clear();
    return;
  }
  loaded_path = state.image_path;
  state.image_pixel_w = static_cast<int>(texture.width);
  state.image_pixel_h = static_cast<int>(texture.height);

  if (state.mode == tools::spritesmith::SheetMode::SpriteSheet && state.columns == 0 && state.frame_width > 0) {
    const int step_x = state.frame_width + state.spacing_x;
    const int step_y = state.frame_height + state.spacing_y;
    state.columns = (state.image_pixel_w - state.offset_x + state.spacing_x) / step_x;
    state.rows = (state.image_pixel_h - state.offset_y + state.spacing_y) / step_y;
  }
}

static void do_open(corundum::toolkit::ToolHost &host, EditorState &state, const std::filesystem::path &path) {
  try {
    tools::spritesmith::load_sheet(state, path);
    state.dirty = false;
    host.set_title("Spritesmith :: " + path.filename().string());
  } catch (const std::exception &e) {
    std::println(stderr, "[Spritesmith] Failed to open {}: {}", path.string(), e.what());
  }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  if (argc > 2) {
    std::println(stderr, "Usage: spritesmith [sheet.json]");
    std::println(stderr, "  Run from the project root directory.");
    return 1;
  }

  auto cfg_result = corundum::toolkit::load_tool_config(argc, argv);
  if (!cfg_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::toolkit::ToolConfig cfg = std::move(*cfg_result);

  EditorState state;
  state.tile_diamond_w = cfg.tile_diamond_w;
  state.tile_diamond_h = cfg.tile_diamond_h;
  if (argc == 2) {
    try {
      tools::spritesmith::load_sheet(state, argv[1]);
    } catch (const std::exception &e) {
      std::println(stderr, "[Spritesmith] FATAL: {}", e.what());
      return 1;
    }
  }

  const std::string title = (argc == 2)
                                ? std::string("Spritesmith :: ") + std::filesystem::path(argv[1]).filename().string()
                                : "Spritesmith :: Untitled";

  auto host_result = corundum::toolkit::ToolHost::create(
      {tools::spritesmith::WINDOW_W, tools::spritesmith::WINDOW_H + tools::spritesmith::MENU_BAR_H, title});
  if (!host_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", host_result.error());
    return 1;
  }
  auto host = std::move(*host_result);

  const corundum::toolkit::FontHandles fonts = load_tool_fonts(cfg);
  ThemeColors theme = ApplyEditorThemeRefined();
  if (!cfg.theme_path.empty()) {
    if (auto t = load_theme(cfg.theme_path.string()))
      theme = *t;
    else
      std::println(stderr, "[Spritesmith] Theme load failed: {} — using fallback", t.error());
  }

  auto checkerboard_result = host->make_checkerboard(static_cast<unsigned>(tools::spritesmith::CANVAS_W),
                                                     static_cast<unsigned>(tools::spritesmith::CANVAS_H), 8);
  if (!checkerboard_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", checkerboard_result.error());
    return 1;
  }
  corundum::platform::TextureInfo checkerboard = *checkerboard_result;

  corundum::platform::TextureInfo sprite_texture{};
  std::string loaded_path;
  bool has_texture = false;

  if (!state.image_path.empty()) {
    try_load_texture(*host, state, sprite_texture, loaded_path);
    has_texture = sprite_texture.width > 0;

    if (has_texture) {
      state.canvas.offset_x = std::max(0.f, static_cast<float>(state.image_pixel_w) * state.canvas.scale * 0.5f -
                                                static_cast<float>(tools::spritesmith::CANVAS_W) * 0.5f);
      state.canvas.offset_y = std::max(0.f, static_cast<float>(state.image_pixel_h) * state.canvas.scale * 0.5f -
                                                static_cast<float>(tools::spritesmith::CANVAS_H) * 0.5f);
    }
  }

  bool running = true;

  host->run([&]() {
    if (!running) {
      host->request_close();
      return;
    }

    const ImGuiIO &io = ImGui::GetIO();

    // Reload texture when the image path changes.
    if (state.image_path != loaded_path) {
      try_load_texture(*host, state, sprite_texture, loaded_path);
      has_texture = sprite_texture.width > 0;
    }

    handle_input(state, running);

    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar(2);

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Sheet...")) {
          const auto start = state.json_path.empty() ? std::filesystem::current_path() : state.json_path.parent_path();
          corundum::toolkit::open_file_browser(state.open_browser, "Open Sprite Sheet", start,
                                               {{"Sprite Sheet JSON", {"json"}}});
        }
        if (ImGui::MenuItem("Save", "Cmd+S"))
          action_save(state);
        if (ImGui::MenuItem("Save As..."))
          open_save_as_browser(state);
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    if (auto picked = corundum::toolkit::render_file_browser(state.open_browser)) {
      if (state.dirty) {
        state.show_open_confirm = true;
        state.pending_open_path = *picked;
      } else {
        do_open(*host, state, *picked);
      }
    }
    if (auto picked = corundum::toolkit::render_file_browser(state.save_browser)) {
      state.json_path = *picked;
      if (auto r = save_sheet(state); !r)
        std::println(stderr, "[Spritesmith] Save failed: {}", r.error());
      else
        host->set_title("Spritesmith :: " + state.json_path.filename().string());
    }

    if (state.show_open_confirm) {
      ImGui::OpenPopup("Unsaved Changes##open");
      state.show_open_confirm = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes##open", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("This sheet has unsaved changes. Open the new sheet anyway and discard them?");
      if (ImGui::Button("Discard & Open", ImVec2{160.f, 0.f})) {
        do_open(*host, state, state.pending_open_path);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{100.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::BeginChild(
        "##canvas",
        {static_cast<float>(tools::spritesmith::CANVAS_W), static_cast<float>(tools::spritesmith::CANVAS_H)}, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();

      // offset_x/offset_y (pan+zoom) live entirely in state.canvas, mutated by
      // CanvasController::update() — this child has no scrollable content and never
      // touches ImGui's own scroll state. (See render_canvas.cpp: draw positions
      // subtract state.canvas.offset_x/y manually; ImGui scroll plays no part.)

      dl->PushClipRect(origin, {origin.x + tools::spritesmith::CANVAS_W, origin.y + tools::spritesmith::CANVAS_H},
                       true);

      dl->AddImage(host->imgui_id(checkerboard.id), origin,
                   {origin.x + tools::spritesmith::CANVAS_W, origin.y + tools::spritesmith::CANVAS_H});

      CanvasContext ctx{dl, origin};
      tools::spritesmith::render_canvas(ctx, state, host->imgui_id(sprite_texture.id), has_texture);

      dl->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0.f, 0.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::BeginChild(
        "##panel", {static_cast<float>(tools::spritesmith::PANEL_W), static_cast<float>(tools::spritesmith::CANVAS_H)},
        false, ImGuiWindowFlags_NoScrollbar);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 sep = ImGui::GetCursorScreenPos();
      dl->AddRectFilled(sep, {sep.x + 1.f, sep.y + static_cast<float>(tools::spritesmith::CANVAS_H)},
                        IM_COL32(80, 80, 100, 255));

      tools::spritesmith::render_side_panel(state, fonts, theme);
      tools::spritesmith::render_preview_panel(*host, state, sprite_texture, io.DeltaTime);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    tools::spritesmith::render_status_bar(state);

    ImGui::End();
  });

  host->textures().destroy(sprite_texture.id);
  host->textures().destroy(checkerboard.id);
  return 0;
}
