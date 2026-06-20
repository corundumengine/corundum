#include "render_preview_panel.hpp"
#include "layout.hpp"
#include "render_anim_preview.hpp"
#include "tool_app.hpp"
#include <imgui.h>

namespace tools::sprite {

  void render_preview_panel(ToolApp &app, const EditorState &state, const ToolTexture &tex, float dt_seconds) {
    using tools::common::tex_id;

    ImGui::SetNextWindowPos({static_cast<float>(CANVAS_W), static_cast<float>(PANEL_H)});
    ImGui::SetNextWindowSize({static_cast<float>(PANEL_W), static_cast<float>(PREVIEW_H)});
    ImGui::Begin("##preview", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::SeparatorText("Preview");

    const bool has_texture = tex.w > 0;
    const bool has_selection =
        (state.mode == SheetMode::Character)
            ? (state.selected_sprite >= 0 && state.selected_sprite < static_cast<int>(state.sprites.size()))
            : (state.selected_clip >= 0 && state.selected_clip < static_cast<int>(state.anim_clips.size()));

    if (!has_texture || !has_selection)
      ImGui::TextDisabled("(select a sprite and animation)");
    else
      render_anim_preview(state, tex_id(app, tex), tex.w, tex.h, has_texture, dt_seconds);

    ImGui::End();
  }

} // namespace tools::sprite
