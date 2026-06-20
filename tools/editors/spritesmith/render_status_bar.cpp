#include "render_status_bar.hpp"
#include "layout.hpp"
#include <format>
#include <imgui.h>

namespace tools::sprite {

  void render_status_bar(const EditorState &state) {
    const ImGuiIO &io = ImGui::GetIO();
    ImGui::SetCursorPos({0.f, static_cast<float>(CANVAS_H)});
    ImGui::BeginChild("##statusbar", {io.DisplaySize.x, static_cast<float>(STATUS_H)}, false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const char *mode_str = (state.mode == SheetMode::Character) ? "Character" : "Sprite Sheet";
    const std::string file = state.json_path.empty() ? "Untitled" : state.json_path.filename().string();

    std::string hover_str;
    if (state.hover_col >= 0) {
      auto gid = state.hover_col + state.hover_row * state.columns;
      hover_str = std::format("  [col:{} row:{} gid:{}]", state.hover_col, state.hover_row, gid);
    }

    const bool recording = (state.mode == SheetMode::Character && state.anim_recording) ||
                           (state.mode == SheetMode::SpriteSheet && state.clip_recording);

    const std::string text = std::format("[{}]  {}{}{}  [Ctrl+S to save]  [Q/Esc to quit]", mode_str, file, hover_str,
                                         recording ? "  [● RECORDING]" : (state.dirty ? "  *" : ""));

    ImGui::SetCursorPosX(15.f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::EndChild();
  }

} // namespace tools::sprite
