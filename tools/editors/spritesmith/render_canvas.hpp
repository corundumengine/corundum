#pragma once
#include "common/canvas_context.hpp"
#include "editor_state.hpp"
#include <imgui.h>

namespace tools::sprite {

  using CanvasContext = tools::common::CanvasContext;

  /**
   * @brief Render the sprite sheet canvas with grid overlay.
   *
   * Draws the loaded sprite sheet texture (scaled and offset by camera),
   * then draws the frame grid and hover/selection highlights.
   * No-op when @p has_texture is false.
   *
   * @param ctx         Canvas draw context.
   * @param state       Current editor state.
   * @param tex         Sprite sheet ImTextureID.
   * @param has_texture True when @p tex is a valid loaded texture.
   */
  void render_canvas(CanvasContext ctx, const EditorState &state, ImTextureID tex, bool has_texture);

} // namespace tools::sprite
