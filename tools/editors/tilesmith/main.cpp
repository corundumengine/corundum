// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "coords.hpp"
#include "editor_state.hpp"
#include <corundum/toolkit/widgets/ui_theme.hpp>

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
#include <corundum/core/content_source.hpp>
#include <corundum/toolkit/editor/canvas_controller.hpp>
#include <corundum/toolkit/host/tool_config.hpp>
#include <corundum/toolkit/host/tool_host.hpp>
#include <corundum/toolkit/widgets/fonts.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <cstdio>
#include <exception>
#include <expected>
#include <imgui.h>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using corundum::toolkit::widgets::ApplyEditorThemeRefined;
using corundum::toolkit::widgets::load_theme;
using corundum::toolkit::widgets::load_tool_fonts;
using corundum::toolkit::widgets::ThemeColors;
using tools::tilesmith::CanvasContext;
using tools::tilesmith::EditorState;

using tools::tilesmith::MapRenderFn;
using tools::tilesmith::MouseState;
using tools::tilesmith::TilemapTextureStore;
using tools::tilesmith::TilesetView;

namespace {

// Compute a canvas tile_scale that targets ~64px rendered cells so large
// tiles (e.g. 256px isometric) don't fill the entire viewport. Small tiles
// are capped at 2x (their historical default).
[[nodiscard]] float compute_tile_scale(int frame_width) noexcept {
  constexpr float k_target_px = 64.f;
  constexpr float k_max_scale = 2.f;
  return std::clamp(k_target_px / static_cast<float>(frame_width), 0.125f, k_max_scale);
}

void center_camera(EditorState &state) noexcept {
  if (state.map.tilesets.empty())
    return;
  state.canvas.set_scale(compute_tile_scale(state.map.diamond_w()));
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

} // namespace

namespace tools::tilesmith {

void finish_map_load(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                     std::vector<TilesetView> &tileset_views) {
  auto tilemap_result =
      corundum::world::tilemap::load_tilemap(corundum::core::DirectoryContentSource{}, state.map_path.string());
  if (!tilemap_result)
    throw std::runtime_error(tilemap_result.error());
  auto new_map = std::move(*tilemap_result);

  // Build everything that can fail before mutating state, so a failed open leaves the
  // currently-loaded map (and the views/store pointing into it) intact rather than
  // dangling. load_portals() reads only map_path, so it can run before the map commit.
  auto new_store = tools::tilesmith::load_tilemap_textures(host, new_map);
  auto portals_result = tools::tilesmith::load_portals(state);
  if (!portals_result)
    throw std::runtime_error(portals_result.error());

  // Commit phase: moves and the view rebuild cannot throw.
  state.map = std::move(new_map);
  state.layer_visible.assign(state.map.layers.size(), true);
  texture_store = std::move(new_store);
  tileset_views = tools::tilesmith::rebuild_tileset_views(host, state.map, texture_store);
  center_camera(state);
  state.undo.clear();
  tools::tilesmith::push_undo_checkpoint(state);
}

} // namespace tools::tilesmith

namespace {

/// Command-line and game.json inputs, plus the map path they imply.
struct Launch {
  corundum::toolkit::host::ToolConfig tool{};

  bool load_map_mode{};

  std::filesystem::path map_path{};
};

[[nodiscard]] std::expected<Launch, std::string> make_launch(std::span<char *> args) {
  auto tool = corundum::toolkit::host::load_tool_config(static_cast<int>(args.size()), args.data());
  if (!tool)
    return std::unexpected(tool.error());
  Launch launch{.tool = std::move(*tool), .load_map_mode = args.size() > 1};
  if (launch.load_map_mode)
    launch.map_path = args[1];
  return launch;
}

[[nodiscard]] std::string window_title(const Launch &launch) {
  if (!launch.load_map_mode)
    return "Tilesmith";
  return "Tilesmith :: " + launch.map_path.filename().string();
}

[[nodiscard]] ThemeColors load_editor_theme(const corundum::toolkit::host::ToolConfig &tool) {
  if (tool.theme_path.empty())
    return ApplyEditorThemeRefined();
  auto loaded = load_theme(tool.theme_path.string());
  if (loaded)
    return *loaded;
  std::println(stderr, "[Tilesmith] Theme load failed: {} — using fallback", loaded.error());
  return ApplyEditorThemeRefined();
}

/// Everything the frame loop reads, so one named function can own a frame.
/// @note The pointers are main()'s objects, live for the whole run and never null.
struct FrameContext {
  corundum::toolkit::host::ToolHost *host{};

  EditorState *state{};

  TilemapTextureStore *texture_store{};

  std::vector<TilesetView> *tileset_views{};

  const ThemeColors *theme{};

  MouseState *mouse{};

  /// Open the New Map dialog on the first frame (no map was named on the command line).
  bool open_new_map_on_first_frame{};

  bool running{true};

  bool dialog_opened{};

  float elapsed_time{};
};

/// Every layer of band @p z_index, in the canvas space of @p ctx.
void render_layer_band(const FrameContext &frame, CanvasContext ctx, int z_index) {
  const EditorState &state = *frame.state;
  tools::tilesmith::render_tilemap(*frame.host, ctx, state.map, state.layer_visible, *frame.texture_store,
                                   state.canvas.offset_x, state.canvas.offset_y, z_index, state.canvas.scale,
                                   frame.elapsed_time, state.elev_step_px);
}

/// The canvas window: the pan/zoom origin shift plus every tilemap pass, clipped to the canvas.
void draw_canvas_window(const FrameContext &frame) {
  const EditorState &state = *frame.state;
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

    const MapRenderFn render_map = [&frame, &state](CanvasContext band_ctx) {
      render_layer_band(frame, band_ctx, 0);
      for (const int z : tools::tilesmith::above_z_indices(state.map))
        render_layer_band(frame, band_ctx, z);
    };
    CanvasContext ctx{.dl = dl, .origin = content_origin};
    tools::tilesmith::render_canvas(ctx, state, render_map);

    dl->PopClipRect();
  }
  ImGui::End();
}

/// The right-hand panel: the separator plus the layer strip.
void draw_panel_window(const FrameContext &frame) {
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

    tools::tilesmith::render_layer_strip(*frame.state, *frame.theme);
  }
  ImGui::End();
}

/// One frame: the menu, both windows, the overlays and the input pass.
void run_frame(FrameContext &frame) {
  if (!frame.running) {
    frame.host->request_close();
    return;
  }

  if (frame.open_new_map_on_first_frame && !frame.dialog_opened) {
    tools::tilesmith::open_new_map_dialog(*frame.state);
    frame.dialog_opened = true;
  }

  frame.elapsed_time += ImGui::GetIO().DeltaTime;

  tools::tilesmith::render_menu_bar(*frame.host, *frame.state, *frame.texture_store, *frame.tileset_views,
                                    frame.running);
  draw_canvas_window(frame);
  draw_panel_window(frame);
  tools::tilesmith::render_tile_grid(*frame.host, *frame.state, *frame.texture_store, *frame.tileset_views);
  tools::tilesmith::render_status_bar(*frame.state, *frame.theme);
  tools::tilesmith::render_portal_panel(*frame.state);
  tools::tilesmith::handle_input(*frame.state, *frame.mouse, frame.running);
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc > 2) {
    std::println(stderr, "Usage: tilesmith [map.json]");
    std::println(stderr, "  Omit map.json to create a new tilemap.");
    std::println(stderr, "  Run from the project root directory.");
    return 1;
  }

  auto launch = make_launch({argv, static_cast<std::size_t>(argc)});
  if (!launch) {
    std::println(stderr, "[Tilesmith] FATAL: {}", launch.error());
    return 1;
  }

  EditorState state;
  state.elev_step_px = launch->tool.elevation_step_px;
  state.max_step_height = launch->tool.max_step_height;
  state.map_path = launch->map_path;

  auto host_result = corundum::toolkit::host::ToolHost::create(
      {.width = tools::tilesmith::WINDOW_W, .height = tools::tilesmith::WINDOW_H, .title = window_title(*launch)});
  if (!host_result) {
    std::println(stderr, "[Tilesmith] FATAL: {}", host_result.error());
    return 1;
  }
  auto host = std::move(*host_result);

  // Fonts must be loaded after the ImGui context is created (inside the ToolHost constructor).
  [[maybe_unused]] const corundum::toolkit::widgets::FontHandles fonts = load_tool_fonts(launch->tool);
  const ThemeColors theme = load_editor_theme(launch->tool);

  TilemapTextureStore texture_store;
  std::vector<TilesetView> tileset_views;

  if (launch->load_map_mode) {
    try {
      tools::tilesmith::finish_map_load(*host, state, texture_store, tileset_views);
    } catch (const std::exception &e) {
      std::println(stderr, "[Tilesmith] FATAL: {}", e.what());
      return 1;
    }
  }

  MouseState mouse;
  FrameContext frame{
      .host = host.get(),
      .state = &state,
      .texture_store = &texture_store,
      .tileset_views = &tileset_views,
      .theme = &theme,
      .mouse = &mouse,
      .open_new_map_on_first_frame = !launch->load_map_mode,
  };
  host->run([&frame] { run_frame(frame); });

  return 0;
}
