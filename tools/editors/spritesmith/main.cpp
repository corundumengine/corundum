#include "editor_state.hpp"
#include "input.hpp"
#include "layout.hpp"
#include "load.hpp"
#include "render_canvas.hpp"
#include "render_preview_panel.hpp"
#include "render_side_panel.hpp"
#include "render_status_bar.hpp"
#include <corundum/tool_host/fonts.hpp>
#include <corundum/tool_host/tool_config.hpp>
#include <corundum/tool_host/tool_host.hpp>
#include <corundum/tool_host/ui_theme.hpp>
#include <imgui.h>
#include <print>
#include <string>

using corundum::tool_host::ApplyEditorThemeRefined;
using corundum::tool_host::load_theme;
using corundum::tool_host::MouseState;
using corundum::tool_host::ThemeColors;
using tools::sprite::CanvasContext;
using tools::sprite::EditorState;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void try_load_texture(corundum::tool_host::ToolHost &host, EditorState &state,
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

  if (state.mode == tools::sprite::SheetMode::SpriteSheet && state.columns == 0 && state.frame_width > 0) {
    const int step_x = state.frame_width + state.spacing_x;
    const int step_y = state.frame_height + state.spacing_y;
    state.columns = (state.image_pixel_w - state.offset_x + state.spacing_x) / step_x;
    state.rows = (state.image_pixel_h - state.offset_y + state.spacing_y) / step_y;
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

  auto cfg_result = corundum::tool_host::load_tool_config(argc, argv);
  if (!cfg_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::tool_host::ToolConfig cfg = std::move(*cfg_result);

  EditorState state;
  if (argc == 2) {
    try {
      tools::sprite::load_sheet(state, argv[1]);
    } catch (const std::exception &e) {
      std::println(stderr, "[Spritesmith] FATAL: {}", e.what());
      return 1;
    }
  }

  const std::string title = (argc == 2)
                                ? std::string("Spritesmith :: ") + std::filesystem::path(argv[1]).filename().string()
                                : "Spritesmith :: Untitled";

  auto host_result = corundum::tool_host::ToolHost::create({tools::sprite::WINDOW_W, tools::sprite::WINDOW_H, title});
  if (!host_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", host_result.error());
    return 1;
  }
  auto host = std::move(*host_result);

  const corundum::tool_host::FontHandles fonts = load_tool_fonts(cfg);
  ThemeColors theme = ApplyEditorThemeRefined();
  if (!cfg.theme_path.empty()) {
    if (auto t = load_theme(cfg.theme_path.string()))
      theme = *t;
    else
      std::println(stderr, "[Spritesmith] Theme load failed: {} — using fallback", t.error());
  }

  corundum::platform::TextureInfo checkerboard = host->make_checkerboard(
      static_cast<unsigned>(tools::sprite::CANVAS_W), static_cast<unsigned>(tools::sprite::CANVAS_H), 8);

  corundum::platform::TextureInfo sprite_texture;
  std::string loaded_path;
  bool has_texture = false;

  if (!state.image_path.empty()) {
    try_load_texture(*host, state, sprite_texture, loaded_path);
    has_texture = sprite_texture.width > 0;

    if (has_texture) {
      state.canvas.offset_x = std::max(0.f, static_cast<float>(state.image_pixel_w) * state.canvas.scale * 0.5f -
                                                static_cast<float>(tools::sprite::CANVAS_W) * 0.5f);
      state.canvas.offset_y = std::max(0.f, static_cast<float>(state.image_pixel_h) * state.canvas.scale * 0.5f -
                                                static_cast<float>(tools::sprite::CANVAS_H) * 0.5f);
    }
  }

  MouseState mouse;
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

    handle_input(state, mouse, running);

    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    if (has_texture) {
      ImGui::SetNextWindowContentSize({static_cast<float>(state.image_pixel_w) * state.canvas.scale,
                                       static_cast<float>(state.image_pixel_h) * state.canvas.scale});
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::BeginChild("##canvas",
                      {static_cast<float>(tools::sprite::CANVAS_W), static_cast<float>(tools::sprite::CANVAS_H)}, false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();

      // Sync ImGui scrollbar with CanvasController
      if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        ImGui::SetScrollX(state.canvas.offset_x);
        ImGui::SetScrollY(state.canvas.offset_y);
      } else {
        state.canvas.offset_x = ImGui::GetScrollX();
        state.canvas.offset_y = ImGui::GetScrollY();
      }

      dl->PushClipRect(origin, {origin.x + tools::sprite::CANVAS_W, origin.y + tools::sprite::CANVAS_H}, true);

      dl->AddImage(host->imgui_id(checkerboard.id), origin,
                   {origin.x + tools::sprite::CANVAS_W, origin.y + tools::sprite::CANVAS_H});

      CanvasContext ctx{dl, origin};
      tools::sprite::render_canvas(ctx, state, host->imgui_id(sprite_texture.id), has_texture);

      dl->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0.f, 0.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::BeginChild("##panel",
                      {static_cast<float>(tools::sprite::PANEL_W), static_cast<float>(tools::sprite::CANVAS_H)}, false,
                      ImGuiWindowFlags_NoScrollbar);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 sep = ImGui::GetCursorScreenPos();
      dl->AddRectFilled(sep, {sep.x + 1.f, sep.y + static_cast<float>(tools::sprite::CANVAS_H)},
                        IM_COL32(80, 80, 100, 255));

      tools::sprite::render_side_panel(state, fonts, theme);
      tools::sprite::render_preview_panel(*host, state, sprite_texture, io.DeltaTime);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    tools::sprite::render_status_bar(state);

    ImGui::End();
  });

  host->textures().destroy(sprite_texture.id);
  host->textures().destroy(checkerboard.id);
  return 0;
}
