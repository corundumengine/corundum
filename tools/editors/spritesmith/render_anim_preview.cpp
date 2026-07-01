#include "render_anim_preview.hpp"
#include <corundum/resources/sprite.hpp>
#include <imgui.h>
#include <span>

namespace tools::sprite {

  namespace {

    using corundum::resources::FrameCoord;

    // Retrieve the active frame sequence (character or sprite sheet mode).
    std::span<const FrameCoord> active_frames(const EditorState &state) {
      if (state.mode == SheetMode::Character) {
        if (state.selected_sprite < 0 || state.selected_sprite >= static_cast<int>(state.sprites.size()))
          return {};
        const auto &sp = state.sprites[static_cast<std::size_t>(state.selected_sprite)];
        return sp.anim_frames[static_cast<int>(state.selected_anim)];
      }
      if (state.selected_clip < 0 || state.selected_clip >= static_cast<int>(state.anim_clips.size()))
        return {};
      return state.anim_clips[static_cast<std::size_t>(state.selected_clip)].frames;
    }

    float frame_duration(const EditorState &state) {
      const int fps = (state.mode == SheetMode::SpriteSheet) ? state.anim_fps : 8;
      return 1.f / static_cast<float>(std::max(1, fps));
    }

    // Return {col_span, row_span} for the active selection (1,1 for sprite sheet mode).
    std::pair<int, int> active_span(const EditorState &state) {
      if (state.mode == SheetMode::Character && state.selected_sprite >= 0 &&
          state.selected_sprite < static_cast<int>(state.sprites.size())) {
        const auto &sp = state.sprites[static_cast<std::size_t>(state.selected_sprite)];
        return {sp.col_span, sp.row_span};
      }
      return {1, 1};
    }

    // Return UV rect {u0,v0,u1,v1} covering pixel_w × pixel_h at frame (col,row).
    std::array<float, 4> frame_uvs(const FrameCoord &fc, const EditorState &state, float tex_w, float tex_h,
                                   int pixel_w, int pixel_h) {
      const corundum::resources::IntPoint origin =
          corundum::resources::frame_origin(state.offset_x, state.offset_y, state.frame_width, state.frame_height,
                                            state.spacing_x, state.spacing_y, fc.col, fc.row);
      const float px = static_cast<float>(origin.x);
      const float py = static_cast<float>(origin.y);
      return {px / tex_w, py / tex_h, (px + static_cast<float>(pixel_w)) / tex_w,
              (py + static_cast<float>(pixel_h)) / tex_h};
    }

  } // namespace

  void render_anim_preview(const EditorState &state, ImTextureID tex, unsigned tex_w, unsigned tex_h, bool has_texture,
                           float dt_seconds) {
    if (!has_texture)
      return;
    const auto frames = active_frames(state);
    if (frames.empty())
      return;

    static float elapsed = 0.f;
    elapsed += dt_seconds;
    const float dur = frame_duration(state);
    const int total = static_cast<int>(frames.size());
    const int fi = static_cast<int>(elapsed / dur) % total;
    const auto &fc = frames[fi];

    const auto [col_span, row_span] = active_span(state);
    const int pixel_w = corundum::resources::rendered_frame_width(col_span, state.frame_width, state.spacing_x);
    const int pixel_h = corundum::resources::rendered_frame_height(row_span, state.frame_height, state.spacing_y);

    const float tw = static_cast<float>(tex_w);
    const float th = static_cast<float>(tex_h);
    const auto uvs = frame_uvs(fc, state, tw, th, pixel_w, pixel_h);

    // Display at 4× native size, capped to 200 px per axis.
    const float disp_w = std::min(static_cast<float>(pixel_w) * 4.f, 200.f);
    const float disp_h = std::min(static_cast<float>(pixel_h) * 4.f, 200.f);

    ImGui::Text("Frame %d / %d  (%d, %d)", fi + 1, total, fc.col, fc.row);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - disp_w) * 0.5f);
    const ImVec2 img_pos = ImGui::GetCursorScreenPos();
    ImGui::Image(tex, {disp_w, disp_h}, {uvs[0], uvs[1]}, {uvs[2], uvs[3]});

    if (state.show_collision_box && state.mode == SheetMode::Character && state.selected_sprite >= 0 &&
        state.selected_sprite < static_cast<int>(state.sprites.size())) {
      const auto &sp = state.sprites[static_cast<std::size_t>(state.selected_sprite)];
      if (sp.collision_w > 0 && sp.collision_h > 0) {
        const float scale_x = disp_w / static_cast<float>(pixel_w);
        const float scale_y = disp_h / static_cast<float>(pixel_h);
        const float coll_disp_w = static_cast<float>(sp.collision_w) * scale_x;
        const float coll_disp_h = static_cast<float>(sp.collision_h) * scale_y;
        const float coll_x = img_pos.x + (disp_w - coll_disp_w) * 0.5f;
        const float coll_y = img_pos.y + coll_disp_h * sp.walk_around_offset;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRect({coll_x, coll_y}, {coll_x + coll_disp_w, coll_y + coll_disp_h}, IM_COL32(255, 80, 80, 220), 0.f, 0,
                    1.5f);
      }
    }
  }

} // namespace tools::sprite
