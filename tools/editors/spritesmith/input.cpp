#include "input.hpp"
#include "coords.hpp"
#include "layout.hpp"
#include "save.hpp"
#include <algorithm>
#include <imgui.h>
#include <print>

namespace tools::sprite {

  namespace {

    int sheet_cols(const EditorState &state) {
      if (state.mode == SheetMode::SpriteSheet)
        return state.columns;
      const int step = state.frame_width + state.spacing_x;
      if (step <= 0 || state.image_pixel_w <= state.offset_x)
        return 0;
      return (state.image_pixel_w - state.offset_x + state.spacing_x) / step;
    }

    int sheet_rows(const EditorState &state) {
      if (state.mode == SheetMode::SpriteSheet)
        return state.rows;
      const int step = state.frame_height + state.spacing_y;
      if (step <= 0 || state.image_pixel_h <= state.offset_y)
        return 0;
      return (state.image_pixel_h - state.offset_y + state.spacing_y) / step;
    }

    bool is_recording(const EditorState &state) {
      return (state.mode == SheetMode::Character && state.anim_recording) ||
             (state.mode == SheetMode::SpriteSheet && state.clip_recording);
    }

    void add_frame(EditorState &state, corundum::resources::FrameCoord fc) {
      if (state.mode == SheetMode::Character) {
        if (state.selected_sprite < 0 || state.selected_sprite >= static_cast<int>(state.sprites.size()))
          return;
        const int ai = static_cast<int>(state.selected_anim);
        state.sprites[static_cast<std::size_t>(state.selected_sprite)].anim_frames[ai].push_back(fc);
      } else {
        if (state.selected_clip < 0 || state.selected_clip >= static_cast<int>(state.anim_clips.size()))
          return;
        state.anim_clips[static_cast<std::size_t>(state.selected_clip)].frames.push_back(fc);
      }
      state.dirty = true;
    }

  } // namespace

  void handle_input(EditorState &state, MouseState &mouse, bool &running) {
    const ImGuiIO &io = ImGui::GetIO();
    const int mx = static_cast<int>(io.MousePos.x);
    const int my = static_cast<int>(io.MousePos.y);
    const bool over_canvas = mx >= 0 && mx < CANVAS_W && my >= 0 && my < CANVAS_H;

    // Camera pan/zoom via CanvasController
    state.canvas.update({0.f, 0.f}, {static_cast<float>(CANVAS_W), static_cast<float>(CANVAS_H)},
                        /*zoom_to_cursor=*/true, 0.25f, 16.f);

    // --- Keyboard ---
    if (!io.WantCaptureKeyboard) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (is_recording(state)) {
          state.anim_recording = false;
          state.clip_recording = false;
        } else {
          running = false;
          return;
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Q) && !io.KeyCtrl) {
        running = false;
        return;
      }
      if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        if (auto r = save_sheet(state); !r)
          std::println(stderr, "[Spritesmith] Save failed: {}", r.error());
        else
          std::println("[Spritesmith] Saved: {}", state.json_path.string());
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        state.canvas.scale = std::min(state.canvas.scale * 1.25f, 16.f);
      if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        state.canvas.scale = std::max(state.canvas.scale / 1.25f, 0.25f);
    }

    // --- Left mouse: frame recording ---
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      mouse.left_held = true;
      if (over_canvas && is_recording(state) && state.image_pixel_w > 0 && !ImGui::IsAnyItemActive()) {
        auto fc =
            screen_to_frame(mx, my, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                            state.canvas.scale, state.frame_width, state.frame_height, state.offset_x, state.offset_y,
                            state.spacing_x, state.spacing_y, sheet_cols(state), sheet_rows(state));
        if (fc)
          add_frame(state, *fc);
      }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
      mouse.left_held = false;

    // --- Hover frame coordinate ---
    state.hover_col = -1;
    state.hover_row = -1;
    if (over_canvas && state.image_pixel_w > 0) {
      auto fc = screen_to_frame(mx, my, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                                state.canvas.scale, state.frame_width, state.frame_height, state.offset_x,
                                state.offset_y, state.spacing_x, state.spacing_y, sheet_cols(state), sheet_rows(state));
      if (fc) {
        state.hover_col = fc->col;
        state.hover_row = fc->row;
      }
    }
  }

} // namespace tools::sprite
