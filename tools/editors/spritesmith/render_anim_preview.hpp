#pragma once
#include "editor_state.hpp"
#include <imgui.h>

namespace tools::sprite {

  /**
   * @brief Render inline animation preview content into the current ImGui window.
   *
   * Cycles through the frames of the active animation or clip at the configured
   * FPS and displays the current frame using UV-mapped ImGui::Image.
   * Does nothing if @p has_texture is false or there are no frames to show.
   *
   * @param state       Current editor state (read-only).
   * @param tex         Sprite sheet ImTextureID.
   * @param tex_w       Texture width in pixels.
   * @param tex_h       Texture height in pixels.
   * @param has_texture True when @p tex is a valid loaded texture.
   * @param dt_seconds  Seconds elapsed since the last frame (for animation stepping).
   */
  void render_anim_preview(const EditorState &state, ImTextureID tex, unsigned tex_w, unsigned tex_h, bool has_texture,
                           float dt_seconds);

} // namespace tools::sprite
