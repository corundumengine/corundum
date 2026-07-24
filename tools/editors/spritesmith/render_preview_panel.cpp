#include "render_preview_panel.hpp"
#include "layout.hpp"
#include "render_anim_preview.hpp"
#include <corundum/tool_host/tool_host.hpp>
#include <imgui.h>

namespace tools::sprite {

  void render_preview_panel(corundum::tool_host::ToolHost &host, const EditorState &state,
                            const corundum::platform::TextureInfo &tex, float dt_seconds) {
    ImGui::SetNextWindowPos({static_cast<float>(CANVAS_W), static_cast<float>(PANEL_H)});
    ImGui::SetNextWindowSize({static_cast<float>(PANEL_W), static_cast<float>(PREVIEW_H)});
    ImGui::Begin("##preview", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::SeparatorText("Preview");

    const bool has_texture = tex.width > 0;
    bool has_selection = false;
    if (state.mode == SheetMode::Character)
      has_selection = state.selected_sprite >= 0 && state.selected_sprite < static_cast<int>(state.sprites.size());
    else if (state.mode == SheetMode::SpriteSheet)
      has_selection = state.selected_clip >= 0 && state.selected_clip < static_cast<int>(state.anim_clips.size());
    else
      has_selection =
          state.selected_atlas_clip >= 0 && state.selected_atlas_clip < static_cast<int>(state.atlas_clips.size());

    if (!has_texture || !has_selection)
      ImGui::TextDisabled("(select a sprite and animation)");
    else
      render_anim_preview(state, host.imgui_id(tex.id), tex.width, tex.height, has_texture, dt_seconds);

    ImGui::End();
  }

} // namespace tools::sprite
