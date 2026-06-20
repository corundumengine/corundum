#include "../../platform/tool_app.hpp"
#include "common/tool_texture.hpp"
#include "common/ui_theme.hpp"
#include "editor_state.hpp"
#include "imgui_fonts.hpp"
#include "input.hpp"
#include "layout.hpp"
#include "load.hpp"
#include "render_canvas.hpp"
#include "render_preview_panel.hpp"
#include "render_side_panel.hpp"
#include "render_status_bar.hpp"
#include <corundum/core/game_config.hpp>
#include <imgui.h>
#include <print>
#include <string>

using tools::common::destroy_tool_texture;
using tools::common::load_tool_texture;
using tools::common::make_checkerboard;
using tools::common::tex_id;
using tools::sprite::CanvasContext;
using tools::sprite::EditorState;
using tools::sprite::FontHandles;
using tools::sprite::MouseState;
using tools::sprite::ToolTexture;
using tools::theme::ApplyEditorThemeRefined;
using tools::theme::load_theme;
using tools::theme::ThemeColors;

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

static void try_load_texture(tools::ToolApp &app, EditorState &state, ToolTexture &texture, std::string &loaded_path) {
  if (state.image_path.empty())
    return;
  if (!loaded_path.empty())
    destroy_tool_texture(app, texture);
  texture = load_tool_texture(app, state.image_path);
  if (texture.w == 0) {
    std::println(stderr, "[Spritesmith] Cannot load image: {}", state.image_path);
    loaded_path.clear();
    return;
  }
  loaded_path = state.image_path;
  state.image_pixel_w = static_cast<int>(texture.w);
  state.image_pixel_h = static_cast<int>(texture.h);

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

  auto cfg_result = corundum::core::load_game_config("tools/tools.json");
  if (!cfg_result) {
    std::println(stderr, "[Spritesmith] FATAL: {}", cfg_result.error());
    return 1;
  }
  corundum::core::GameConfig cfg = std::move(*cfg_result);

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

  tools::ToolApp app{tools::sprite::WINDOW_W, tools::sprite::WINDOW_H, title};

  // Fonts must be loaded after ImGui context is created (inside ToolApp ctor).
  const FontHandles fonts = load_fonts(cfg);

  ThemeColors theme;
  if (auto t = load_theme("tools/editors/common/editor_dark.json"); t)
    theme = *t;
  else {
    std::println(stderr, "[Spritesmith] Theme load failed: {} — using fallback", t.error());
    theme = ApplyEditorThemeRefined();
  }

  ToolTexture checkerboard = make_checkerboard(app, static_cast<unsigned>(tools::sprite::CANVAS_W),
                                               static_cast<unsigned>(tools::sprite::CANVAS_H), 8);

  ToolTexture sprite_texture;
  std::string loaded_path;
  bool has_texture = false;

  if (!state.image_path.empty()) {
    try_load_texture(app, state, sprite_texture, loaded_path);
    has_texture = sprite_texture.w > 0;

    if (has_texture) {
      state.camera_x = std::max(0.f, static_cast<float>(state.image_pixel_w) * state.zoom * 0.5f -
                                         static_cast<float>(tools::sprite::CANVAS_W) * 0.5f);
      state.camera_y = std::max(0.f, static_cast<float>(state.image_pixel_h) * state.zoom * 0.5f -
                                         static_cast<float>(tools::sprite::CANVAS_H) * 0.5f);
    }
  }

  MouseState mouse;
  bool running = true;

  app.run([&]() {
    if (!running) {
      app.close();
      return;
    }

    const ImGuiIO &io = ImGui::GetIO();

    // Reload texture when the image path changes.
    if (state.image_path != loaded_path) {
      try_load_texture(app, state, sprite_texture, loaded_path);
      has_texture = sprite_texture.w > 0;
    }

    handle_input(state, mouse, running);

    // Full-screen ImGui window containing canvas + panel side by side.
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    // Canvas child window
    if (has_texture) {
      ImGui::SetNextWindowContentSize(
          {static_cast<float>(state.image_pixel_w) * state.zoom, static_cast<float>(state.image_pixel_h) * state.zoom});
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::BeginChild("##canvas",
                      {static_cast<float>(tools::sprite::CANVAS_W), static_cast<float>(tools::sprite::CANVAS_H)}, false,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImDrawList *dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();

      if (state.panning) {
        ImGui::SetScrollX(state.camera_x);
        ImGui::SetScrollY(state.camera_y);
      } else {
        state.camera_x = ImGui::GetScrollX();
        state.camera_y = ImGui::GetScrollY();
      }

      dl->PushClipRect(origin, {origin.x + tools::sprite::CANVAS_W, origin.y + tools::sprite::CANVAS_H}, true);

      dl->AddImage(tex_id(app, checkerboard), origin,
                   {origin.x + tools::sprite::CANVAS_W, origin.y + tools::sprite::CANVAS_H});

      CanvasContext ctx{dl, origin};
      tools::sprite::render_canvas(ctx, state, tex_id(app, sprite_texture), has_texture);

      dl->PopClipRect();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::SameLine(0.f, 0.f);

    // Panel child window
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
      tools::sprite::render_preview_panel(app, state, sprite_texture, io.DeltaTime);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    tools::sprite::render_status_bar(state);

    ImGui::End();
  });

  destroy_tool_texture(app, sprite_texture);
  destroy_tool_texture(app, checkerboard);
  return 0;
}
