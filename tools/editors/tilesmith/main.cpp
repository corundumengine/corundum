#include "coords.hpp"
#include "editor_state.hpp"
#include <corundum/tool_host/ui_theme.hpp>

#include "input.hpp"
#include "layout.hpp"
#include "menu.hpp"
#include "render_canvas.hpp"
#include "render_layer_strip.hpp"
#include "render_portal.hpp"
#include "render_status_bar.hpp"
#include "render_tile_grid.hpp"
#include "save.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"
#include "undo.hpp"
#include <algorithm>
#include <corundum/tool_host/fonts.hpp>
#include <corundum/tool_host/tool_config.hpp>
#include <corundum/tool_host/tool_host.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <cstdio>
#include <imgui.h>
#include <print>
#include <string>
#include <vector>

using corundum::tool_host::ApplyEditorThemeRefined;
using corundum::tool_host::load_theme;
using corundum::tool_host::ThemeColors;
using tools::tilesmith::CanvasContext;
using tools::tilesmith::EditorState;

using tools::tilesmith::load_tilemap_textures;
using tools::tilesmith::MapRenderFn;
using tools::tilesmith::MouseState;
using tools::tilesmith::TilemapTextureStore;
using tools::tilesmith::TilesetView;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// load_fonts replaced by corundum::tool_host::load_tool_fonts(config)

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Compute a canvas tile_scale that targets ~64px rendered cells so large
// tiles (e.g. 256px isometric) don't fill the entire viewport. Small tiles
// are capped at 2x (their historical default).
[[nodiscard]] static float compute_tile_scale(int frame_width) noexcept {
  constexpr float k_target_px = 64.f;
  constexpr float k_max_scale = 2.f;
  return std::clamp(k_target_px / static_cast<float>(frame_width), 0.125f, k_max_scale);
}

static void center_camera(EditorState &state) noexcept {
  if (state.map.tilesets.empty())
    return;
  state.canvas.scale = compute_tile_scale(state.map.diamond_w());
  const float half_tw = static_cast<float>(state.map.diamond_w()) * state.canvas.scale * 0.5f;
  const float half_th = static_cast<float>(state.map.diamond_h()) * state.canvas.scale * 0.5f;
  const float virtual_w = static_cast<float>(state.map.width + state.map.height) * half_tw;
  const float virtual_h = static_cast<float>(state.map.width + state.map.height) * half_th;
  state.canvas.offset_x = (virtual_w - static_cast<float>(tools::tilesmith::CANVAS_W)) * 0.5f;
  state.canvas.offset_y = (virtual_h - static_cast<float>(tools::tilesmith::CANVAS_H)) * 0.5f;
  auto [cx, cy] = tools::tilesmith::clamp_camera(
      state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width, state.map.height,
      state.map.diamond_w(), state.map.diamond_h(), tools::tilesmith::CANVAS_W, tools::tilesmith::CANVAS_H);
  state.canvas.offset_x = cx;
  state.canvas.offset_y = cy;
}

// ---------------------------------------------------------------------------
// New-map setup: load map + portals + textures, center camera, update title.
// Called once from inside the frame loop after JSON is written.
// ---------------------------------------------------------------------------

namespace tools::tilesmith {

  void finish_map_load(corundum::tool_host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                       std::vector<TilesetView> &tileset_views) {
    auto tilemap_result = corundum::world::tilemap::load_tilemap(state.map_path.string());
    if (!tilemap_result)
      throw std::runtime_error(tilemap_result.error());
    state.map = std::move(*tilemap_result);
    auto portals_result = tools::tilesmith::load_portals(state);
    if (!portals_result)
      throw std::runtime_error(portals_result.error());
    texture_store = tools::tilesmith::load_tilemap_textures(host, state.map);
    tileset_views = tools::tilesmith::rebuild_tileset_views(host, state.map, texture_store);
    center_camera(state);
    state.undo.clear();
    tools::tilesmith::push_undo_checkpoint(state);
  }

} // namespace tools::tilesmith

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
  const bool load_map_mode = (argc > 1);

  auto cfg_result = corundum::tool_host::load_tool_config(argc, argv);
  if (!cfg_result) {
    std::println(stderr, "[Tilesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::tool_host::ToolConfig cfg = std::move(*cfg_result);

  EditorState state;
  state.elev_step_px = cfg.elevation_step_px;
  state.max_step_height = cfg.max_step_height;

  if (load_map_mode)
    state.map_path = argv[1];

  const std::string initial_title =
      load_map_mode ? ("Tilesmith :: " + state.map_path.filename().string()) : "Tilesmith";

  auto host_result =
      corundum::tool_host::ToolHost::create({tools::tilesmith::WINDOW_W, tools::tilesmith::WINDOW_H, initial_title});
  if (!host_result) {
    std::println(stderr, "[Tilesmith] FATAL: {}", host_result.error());
    return 1;
  }
  auto host = std::move(*host_result);

  // Fonts must be loaded after ImGui context is created (inside ToolHost ctor).
  [[maybe_unused]] corundum::tool_host::FontHandles fonts = load_tool_fonts(cfg);

  ThemeColors theme;
  if (!cfg.theme_path.empty()) {
    auto t = load_theme(cfg.theme_path.string());
    if (t)
      theme = *t;
    else {
      std::println(stderr, "[Tilesmith] Theme load failed: {} — using fallback", t.error());
      theme = ApplyEditorThemeRefined();
    }
  } else {
    theme = ApplyEditorThemeRefined();
  }

  TilemapTextureStore texture_store;
  std::vector<TilesetView> tileset_views;

  if (load_map_mode) {
    try {
      tools::tilesmith::finish_map_load(*host, state, texture_store, tileset_views);
    } catch (const std::exception &e) {
      std::println(stderr, "[Tilesmith] FATAL: {}", e.what());
      return 1;
    }
  }

  MouseState mouse;
  bool running = true;
  float elapsed_time = 0.f;
  bool triggered_initial_new_map = false;

  // Closure that renders all tilemap passes into a canvas context.
  const MapRenderFn render_map = [&host, &state, &texture_store, &elapsed_time](CanvasContext ctx) {
    tools::tilesmith::render_tilemap(*host, ctx, state.map, texture_store, state.canvas.offset_x, state.canvas.offset_y,
                                     0, state.canvas.scale, elapsed_time, state.elev_step_px);
    for (const int z : tools::tilesmith::above_z_indices(state.map))
      tools::tilesmith::render_tilemap(*host, ctx, state.map, texture_store, state.canvas.offset_x,
                                       state.canvas.offset_y, z, state.canvas.scale, elapsed_time, state.elev_step_px);
  };

  host->run([&]() {
    if (!running) {
      host->request_close();
      return;
    }

    if (!load_map_mode && !triggered_initial_new_map) {
      tools::tilesmith::open_new_map_dialog(state);
      triggered_initial_new_map = true;
    }

    const ImGuiIO &io = ImGui::GetIO();
    elapsed_time += io.DeltaTime;

    tools::tilesmith::render_menu_bar(*host, state, texture_store, tileset_views, running);

    ImGui::SetNextWindowPos({0.f, static_cast<float>(tools::tilesmith::k_menu_h)});
    ImGui::SetNextWindowSize(
        {static_cast<float>(tools::tilesmith::CANVAS_W), static_cast<float>(tools::tilesmith::CANVAS_H)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##canvas", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();
      ImVec2 content_origin = origin;

      // offset_x/offset_y (pan+zoom) live entirely in state.canvas, mutated by
      // CanvasController::update() — this window has no scrollable content and never
      // touches ImGui's own scroll state. (See tilemap_rendering.cpp: tile screen
      // positions subtract camera_x/camera_y manually; ImGui scroll plays no part.)
      if (!state.map.tilesets.empty()) {
        const float half_tw = static_cast<float>(state.map.diamond_w()) * state.canvas.scale * 0.5f;
        const float half_th = static_cast<float>(state.map.diamond_h()) * state.canvas.scale * 0.5f;
        content_origin.x += tools::tilesmith::k_content_margin * half_tw;
        content_origin.y += half_th;
      }

      dl->PushClipRect(origin, {origin.x + tools::tilesmith::CANVAS_W, origin.y + tools::tilesmith::CANVAS_H}, true);

      CanvasContext ctx{dl, content_origin};
      tools::tilesmith::render_canvas(ctx, state, render_map);

      dl->PopClipRect();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(
        {static_cast<float>(tools::tilesmith::CANVAS_W), static_cast<float>(tools::tilesmith::k_menu_h)});
    ImGui::SetNextWindowSize(
        {static_cast<float>(tools::tilesmith::PALETTE_W), static_cast<float>(tools::tilesmith::CANVAS_H)});
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
      dl->AddRectFilled(sep, {sep.x + 1.f, sep.y + tools::tilesmith::CANVAS_H}, IM_COL32(80, 80, 100, 255));

      tools::tilesmith::render_layer_strip(state, theme);
    }
    ImGui::End();

    tools::tilesmith::render_tile_grid(*host, state, texture_store, tileset_views);
    tools::tilesmith::render_status_bar(state, theme);
    tools::tilesmith::render_portal_panel(state);

    tools::tilesmith::handle_input(state, mouse, running);
  });

  return 0;
}
