#include "render_canvas.hpp"
#include "coords.hpp"
#include "layout.hpp"

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

    bool is_in_active_anim(const EditorState &state, int col, int row) {
      if (state.mode == SheetMode::Character) {
        if (state.selected_sprite < 0 || state.selected_sprite >= static_cast<int>(state.sprites.size()))
          return false;
        const auto &sp = state.sprites[static_cast<std::size_t>(state.selected_sprite)];
        for (const auto &fc : sp.anim_frames[static_cast<int>(state.selected_anim)])
          if (fc.col == col && fc.row == row)
            return true;
      } else {
        if (state.selected_clip < 0 || state.selected_clip >= static_cast<int>(state.anim_clips.size()))
          return false;
        for (const auto &fc : state.anim_clips[static_cast<std::size_t>(state.selected_clip)].frames)
          if (fc.col == col && fc.row == row)
            return true;
      }
      return false;
    }

    bool is_recording(const EditorState &state) {
      return (state.mode == SheetMode::Character && state.anim_recording) ||
             (state.mode == SheetMode::SpriteSheet && state.clip_recording);
    }

    void draw_grid(CanvasContext ctx, const EditorState &state, ImTextureID tex, unsigned tex_w, unsigned tex_h) {
      const int cols = sheet_cols(state);
      const int rows = sheet_rows(state);
      if (cols <= 0 || rows <= 0)
        return;

      const bool recording = is_recording(state);

      for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
          const auto rect = frame_to_canvas_rect(c, r, state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale,
                                                 state.frame_width, state.frame_height, state.offset_x, state.offset_y,
                                                 state.spacing_x, state.spacing_y);

          if (rect.x + rect.w < 0.f || rect.x > static_cast<float>(CANVAS_W) || rect.y + rect.h < 0.f ||
              rect.y > static_cast<float>(CANVAS_H))
            continue;

          const ImVec2 p0 = {ctx.origin.x + rect.x, ctx.origin.y + rect.y};
          const ImVec2 p1 = {p0.x + rect.w, p0.y + rect.h};

          ctx.dl->AddRect(p0, p1, IM_COL32(80, 80, 120, 160), 0.f, 0, 1.f);

          if (is_in_active_anim(state, c, r))
            ctx.dl->AddRectFilled(p0, p1, IM_COL32(0, 180, 255, 55));

          if (state.hover_col == c && state.hover_row == r) {
            const ImU32 fill = recording ? IM_COL32(255, 200, 0, 80) : IM_COL32(255, 255, 255, 40);
            const ImU32 outline = recording ? IM_COL32(255, 200, 0, 220) : IM_COL32(200, 200, 200, 160);
            ctx.dl->AddRectFilled(p0, p1, fill);
            ctx.dl->AddRect(p0, p1, outline, 0.f, 0, 2.f);
          }
        }
      }
      (void)tex;
      (void)tex_w;
      (void)tex_h;
    }

  } // namespace

  void render_canvas(CanvasContext ctx, const EditorState &state, ImTextureID tex, bool has_texture) {
    if (!has_texture)
      return;

    // Draw sprite sheet scaled + panned
    const float img_w = static_cast<float>(state.image_pixel_w) * state.canvas.scale;
    const float img_h = static_cast<float>(state.image_pixel_h) * state.canvas.scale;
    const ImVec2 p0 = {ctx.origin.x - state.canvas.offset_x, ctx.origin.y - state.canvas.offset_y};
    const ImVec2 p1 = {p0.x + img_w, p0.y + img_h};
    ctx.dl->AddImage(tex, p0, p1);

    if (state.frame_width > 0 && state.frame_height > 0)
      draw_grid(ctx, state, tex, 0, 0);
  }

} // namespace tools::sprite
