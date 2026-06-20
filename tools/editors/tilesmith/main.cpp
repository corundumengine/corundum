#include "common/ui_theme.hpp"
#include "coords.hpp"
#include "editor_state.hpp"
#include "imgui_fonts.hpp"
#include "input.hpp"
#include "layout.hpp"
#include "new_map_dialog.hpp"
#include "render_canvas.hpp"
#include "render_layer_strip.hpp"
#include "render_portal.hpp"
#include "render_status_bar.hpp"
#include "render_tile_grid.hpp"
#include "save.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"
#include "tool_app.hpp"
#include <algorithm>
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <cstdio>
#include <imgui.h>
#include <print>
#include <string>
#include <vector>

using tools::theme::ApplyEditorThemeRefined;
using tools::theme::load_theme;
using tools::theme::ThemeColors;
using tools::tilemap::CanvasContext;
using tools::tilemap::EditorState;
using tools::tilemap::FontHandles;
using tools::tilemap::load_tilemap_textures;
using tools::tilemap::MapRenderFn;
using tools::tilemap::MouseState;
using tools::tilemap::TilemapTextureStore;
using tools::tilemap::TilesetView;
using tools::tilemap::ToolTexture;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

[[nodiscard]] static FontHandles load_fonts(const corundum::core::GameConfig &cfg) {
  static constexpr ImWchar ranges[] = {0x0020, 0xFFFF, 0};
  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  const auto ui_path = std::format("{}/{}", cfg.paths.font_dir, cfg.paths.ui_font);
  const auto icons_path = std::format("{}/{}", cfg.paths.font_dir, cfg.paths.icons_font);
  FontHandles fonts;
  fonts.ui = io.Fonts->AddFontFromFileTTF(ui_path.c_str(), 18.0f);
  fonts.icons = io.Fonts->AddFontFromFileTTF(icons_path.c_str(), 26.0f, nullptr, ranges);
  return fonts;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Compute a canvas tile_scale that targets ~64px rendered cells so large
// tiles (e.g. 256px isometric) don't fill the entire viewport. Small tiles
// are capped at 2x (their historical default).
[[nodiscard]] static float compute_tile_scale(int tile_width) noexcept {
  constexpr float k_target_px = 64.f;
  constexpr float k_max_scale = 2.f;
  return std::clamp(k_target_px / static_cast<float>(tile_width), 0.125f, k_max_scale);
}

static void center_camera(EditorState &state) noexcept {
  if (state.map.tilesets.empty())
    return;
  state.tile_scale = compute_tile_scale(state.map.diamond_w());
  const float half_tw = static_cast<float>(state.map.diamond_w()) * state.tile_scale * 0.5f;
  const float half_th = static_cast<float>(state.map.diamond_h()) * state.tile_scale * 0.5f;
  const float virtual_w = static_cast<float>(state.map.width + state.map.height) * half_tw;
  const float virtual_h = static_cast<float>(state.map.width + state.map.height) * half_th;
  state.camera.x = (virtual_w - static_cast<float>(tools::tilemap::CANVAS_W)) * 0.5f;
  state.camera.y = (virtual_h - static_cast<float>(tools::tilemap::CANVAS_H)) * 0.5f;
  state.camera = tools::tilemap::clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height,
                                              state.map.diamond_w(), state.map.diamond_h(), tools::tilemap::CANVAS_W,
                                              tools::tilemap::CANVAS_H);
}

// ---------------------------------------------------------------------------
// New-map setup: load map + portals + textures, center camera, update title.
// Called once from inside the frame loop after JSON is written.
// ---------------------------------------------------------------------------

static void finish_map_load(tools::ToolApp &app, EditorState &state, TilemapTextureStore &texture_store,
                            std::vector<TilesetView> &tileset_views) {
  auto tilemap_result = corundum::gameplay::world::tilemap::load_tilemap(state.map_path.string());
  if (!tilemap_result)
    throw std::runtime_error(tilemap_result.error());
  state.map = std::move(*tilemap_result);
  tools::tilemap::load_portals(state);
  texture_store = tools::tilemap::load_tilemap_textures(app, state.map);
  tileset_views = tools::tilemap::rebuild_tileset_views(app, state.map, texture_store);
  center_camera(state);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  if (argc > 2) {
    std::println(stderr, "Usage: tilesmith [map.json]");
    std::println(stderr, "  Omit map.json to create a new tilemap.");
    std::println(stderr, "  Run from the project root directory.");
    return 1;
  }
  const bool new_map_mode = (argc == 1);

  auto cfg_result = corundum::core::load_game_config("tools/tools.json");
  if (!cfg_result) {
    std::println(stderr, "[Tilesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::core::GameConfig cfg = std::move(*cfg_result);

  EditorState state;

  if (!new_map_mode) {
    state.map_path = argv[1];
    auto tilemap_result = corundum::gameplay::world::tilemap::load_tilemap(state.map_path.string());
    if (!tilemap_result) {
      std::println(stderr, "[Tilesmith] FATAL: {}", tilemap_result.error());
      return 1;
    }
    state.map = std::move(*tilemap_result);

    try {
      tools::tilemap::load_portals(state);
    } catch (const std::runtime_error &e) {
      std::println(stderr, "[Tilesmith] FATAL: {}", e.what());
      return 1;
    }
  }

  const std::string initial_title = new_map_mode ? "Tilesmith" : ("Tilesmith :: " + state.map_path.filename().string());
  tools::ToolApp app{tools::tilemap::WINDOW_W, tools::tilemap::WINDOW_H, initial_title};

  // Fonts must be loaded after ImGui context is created (inside ToolApp ctor).
  const FontHandles fonts = load_fonts(cfg);

  ThemeColors theme;
  if (auto t = load_theme("tools/editors/common/editor_dark.json"); t)
    theme = *t;
  else {
    std::println(stderr, "[Tilesmith] Theme load failed: {} — using fallback", t.error());
    theme = ApplyEditorThemeRefined();
  }

  TilemapTextureStore texture_store;
  std::vector<TilesetView> tileset_views;

  if (!new_map_mode) {
    try {
      texture_store = load_tilemap_textures(app, state.map);
    } catch (const std::runtime_error &e) {
      std::println(stderr, "[Tilesmith] FATAL: {}", e.what());
      return 1;
    }

    tileset_views = rebuild_tileset_views(app, state.map, texture_store);
    center_camera(state);
  }

  MouseState mouse;
  bool running = true;
  bool map_ready = !new_map_mode;
  float elapsed_time = 0.f;
  tools::tilemap::NewMapDialogState new_map_dlg{};

  // Closure that renders all tilemap passes into a canvas context.
  const MapRenderFn render_map = [&](CanvasContext ctx) {
    tools::tilemap::render_tilemap(app, ctx, state.map, texture_store, state.camera, 0, state.tile_scale, elapsed_time);
    for (const int z : tools::tilemap::above_z_indices(state.map))
      tools::tilemap::render_tilemap(app, ctx, state.map, texture_store, state.camera, z, state.tile_scale,
                                     elapsed_time);
  };

  app.run([&]() {
    // ── New-map dialog (shown before the editor when launched with no args) ──
    if (!map_ready) {
      tools::tilemap::render_new_map_dialog(new_map_dlg);
      if (new_map_dlg.cancelled) {
        running = false;
        app.close();
        return;
      }
      if (new_map_dlg.confirmed) {
        auto write_result = tools::tilemap::write_new_tilemap_json(new_map_dlg);
        if (!write_result) {
          new_map_dlg.error_msg = write_result.error();
          new_map_dlg.confirmed = false;
          return;
        }
        state.map_path = *write_result;
        try {
          finish_map_load(app, state, texture_store, tileset_views);
          app.set_title("Tilesmith :: " + state.map_path.filename().string());
        } catch (const std::exception &e) {
          new_map_dlg.error_msg = e.what();
          new_map_dlg.confirmed = false;
          return;
        }
        map_ready = true;
      }
      return;
    }

    if (!running) {
      app.close();
      return;
    }

    const ImGuiIO &io = ImGui::GetIO();
    elapsed_time += io.DeltaTime;

    if (!state.map.tilesets.empty()) {
      const int tw = state.map.diamond_w();
      const float half_tw = static_cast<float>(tw) * state.tile_scale * 0.5f;
      const float half_th = static_cast<float>(state.map.diamond_h()) * state.tile_scale * 0.5f;
      ImGui::SetNextWindowContentSize({static_cast<float>(state.map.width + state.map.height) * half_tw,
                                       static_cast<float>(state.map.width + state.map.height) * half_th});
    }
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(
        {static_cast<float>(tools::tilemap::CANVAS_W), static_cast<float>(tools::tilemap::CANVAS_H)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    // NoDecoration = NoTitleBar | NoResize | NoScrollbar | NoCollapse — we omit NoScrollbar to allow scrollbars
    ImGui::Begin("##canvas", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar(2);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();

      if (!state.map.tilesets.empty()) {
        const float half_tw = static_cast<float>(state.map.diamond_w()) * state.tile_scale * 0.5f;
        const float half_th = static_cast<float>(state.map.diamond_h()) * state.tile_scale * 0.5f;
        const float virtual_w = static_cast<float>(state.map.width + state.map.height) * half_tw;
        const float virtual_h = static_cast<float>(state.map.width + state.map.height) * half_th;
        if (state.panning || state.camera_moved) {
          if (virtual_w > static_cast<float>(tools::tilemap::CANVAS_W))
            ImGui::SetScrollX(state.camera.x);
          if (virtual_h > static_cast<float>(tools::tilemap::CANVAS_H))
            ImGui::SetScrollY(state.camera.y);
        } else {
          if (virtual_w > static_cast<float>(tools::tilemap::CANVAS_W))
            state.camera.x = ImGui::GetScrollX();
          if (virtual_h > static_cast<float>(tools::tilemap::CANVAS_H))
            state.camera.y = ImGui::GetScrollY();
        }
      }

      dl->PushClipRect(origin, {origin.x + tools::tilemap::CANVAS_W, origin.y + tools::tilemap::CANVAS_H}, true);

      CanvasContext ctx{dl, origin};
      tools::tilemap::render_canvas(ctx, state, render_map);

      dl->PopClipRect();
    }
    ImGui::End();

    ImGui::SetNextWindowPos({static_cast<float>(tools::tilemap::CANVAS_W), 0.f});
    ImGui::SetNextWindowSize(
        {static_cast<float>(tools::tilemap::PALETTE_W), static_cast<float>(tools::tilemap::CANVAS_H)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMouseInputs);
    ImGui::PopStyleVar(2);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 sep = ImGui::GetCursorScreenPos();
      dl->AddRectFilled(sep, {sep.x + 1.f, sep.y + tools::tilemap::CANVAS_H}, IM_COL32(80, 80, 100, 255));

      tools::tilemap::render_layer_strip(state, fonts, theme);
    }
    ImGui::End();

    tools::tilemap::render_tile_grid(app, state, texture_store, tileset_views);
    tools::tilemap::render_status_bar(state);
    tools::tilemap::render_portal_panel(state);

    tools::tilemap::handle_input(state, mouse, running);
  });

  return 0;
}
