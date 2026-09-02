#include "input.hpp"
#include "coords.hpp"
#include "editor_helpers.hpp"
#include "layout.hpp"
#include "save.hpp"
#include <algorithm>
#include <imgui.h>
#include <optional>
#include <print>

namespace tools::spritesmith {

  namespace {

    void add_frame(EditorState &state, corundum::sprites::FrameCoord fc) {
      if (state.mode == SheetMode::Character) {
        if (state.selected_sprite < 0 || state.selected_sprite >= static_cast<int>(state.sprites.size()))
          return;
        const int anim_index = static_cast<int>(state.selected_anim);
        state.sprites[static_cast<std::size_t>(state.selected_sprite)].anim_frames[anim_index].push_back(fc);
      } else {
        if (state.selected_clip < 0 || state.selected_clip >= static_cast<int>(state.anim_clips.size()))
          return;
        state.anim_clips[static_cast<std::size_t>(state.selected_clip)].frames.push_back(fc);
      }
      state.dirty = true;
    }

    void push_atlas_undo(EditorState &state) {
      state.atlas_undo.push({.clips = state.atlas_clips, .selected_clip = state.selected_atlas_clip});
    }

    void add_atlas_frame(EditorState &state, int sprite_index) {
      if (state.selected_atlas_clip < 0 || state.selected_atlas_clip >= static_cast<int>(state.atlas_clips.size()))
        return;
      if (sprite_index < 0 || sprite_index >= static_cast<int>(state.atlas_sprites.size()))
        return;
      push_atlas_undo(state);
      state.atlas_clips[static_cast<std::size_t>(state.selected_atlas_clip)].frames.push_back(
          state.atlas_sprites[static_cast<std::size_t>(sprite_index)].name);
      state.dirty = true;
    }

    void handle_atlas_undo_redo(EditorState &state) {
      const ImGuiIO &io = ImGui::GetIO();
      auto apply_if = [&state](auto op) {
        AtlasClipDoc doc;
        if ((state.atlas_undo.*op)(doc)) {
          state.atlas_clips = std::move(doc.clips);
          state.selected_atlas_clip = doc.selected_clip;
          state.dirty = true;
        }
      };
      if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
        apply_if(&corundum::tool_host::UndoStack<AtlasClipDoc>::redo);
      else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        apply_if(&corundum::tool_host::UndoStack<AtlasClipDoc>::undo);
    }

    void handle_save(EditorState &state) {
      const ImGuiIO &io = ImGui::GetIO();
      if (!io.KeyCtrl || !ImGui::IsKeyPressed(ImGuiKey_S))
        return;
      if (auto r = save_sheet(state); !r)
        std::println(stderr, "[Spritesmith] Save failed: {}", r.error());
      else
        std::println("[Spritesmith] Saved: {}", state.json_path.string());
    }

    void handle_keyboard(EditorState &state, bool &running) {
      const ImGuiIO &io = ImGui::GetIO();
      if (io.WantCaptureKeyboard)
        return;
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (is_recording(state)) {
          state.anim_recording = false;
          state.clip_recording = false;
          state.atlas_clip_recording = false;
        } else {
          running = false;
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Q) && !io.KeyCtrl) {
        running = false;
        return;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))
        state.canvas.scale = std::min(state.canvas.scale * 1.25f, 16.f);
      if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))
        state.canvas.scale = std::max(state.canvas.scale / 1.25f, 0.25f);
      if (state.mode == SheetMode::Atlas)
        handle_atlas_undo_redo(state);
    }

    [[nodiscard]] std::optional<corundum::sprites::FrameCoord> frame_under_cursor(const EditorState &state, int mx,
                                                                                  int my) {
      return screen_to_frame(mx, my, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                             state.canvas.scale, state.frame_width, state.frame_height, state.offset_x, state.offset_y,
                             state.spacing_x, state.spacing_y, sheet_cols(state), sheet_rows(state));
    }

    [[nodiscard]] int atlas_sprite_under_cursor(const EditorState &state, int mx, int my) {
      return sprite_at_point(mx, my, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                             state.canvas.scale, state.atlas_sprites);
    }

    void handle_mouse_click(EditorState &state, int mx, int my, bool over_canvas) {
      if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;
      if (!over_canvas || !is_recording(state) || state.image_pixel_w <= 0 || ImGui::IsAnyItemActive())
        return;
      if (state.mode == SheetMode::Atlas) {
        const int hit = atlas_sprite_under_cursor(state, mx, my);
        if (hit >= 0)
          add_atlas_frame(state, hit);
      } else {
        auto fc = frame_under_cursor(state, mx, my);
        if (fc)
          add_frame(state, *fc);
      }
    }

    void update_hover(EditorState &state, int mx, int my, bool over_canvas) {
      state.hover_col = -1;
      state.hover_row = -1;
      state.hover_sprite = -1;
      if (!over_canvas)
        return;
      if (state.mode == SheetMode::Atlas) {
        state.hover_sprite = atlas_sprite_under_cursor(state, mx, my);
      } else if (state.image_pixel_w > 0) {
        auto fc = frame_under_cursor(state, mx, my);
        if (fc) {
          state.hover_col = fc->col;
          state.hover_row = fc->row;
        }
      }
    }

  } // namespace

  void handle_input(EditorState &state, bool &running) {
    const ImGuiIO &io = ImGui::GetIO();
    const int mx = static_cast<int>(io.MousePos.x);
    const int my = static_cast<int>(io.MousePos.y);
    const bool over_canvas = mx >= 0 && mx < CANVAS_W && my >= 0 && my < CANVAS_H;

    state.canvas.update({0.f, 0.f}, {static_cast<float>(CANVAS_W), static_cast<float>(CANVAS_H)},
                        /*zoom_to_cursor=*/true, 0.25f, 16.f);
    handle_save(state);
    handle_keyboard(state, running);
    if (!running)
      return;
    handle_mouse_click(state, mx, my, over_canvas);
    update_hover(state, mx, my, over_canvas);
  }

} // namespace tools::spritesmith
