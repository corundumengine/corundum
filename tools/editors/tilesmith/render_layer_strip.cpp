#include "render_layer_strip.hpp"
#include "layout.hpp"
#include <imgui.h>

namespace tools::tilemap {

  using namespace tools::theme;

  void render_layer_strip(const EditorState &state, const FontHandles &fonts, const ThemeColors &theme) {
    const int n_layers = static_cast<int>(state.map.layers.size());
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetCursorScreenPos();

    // Title row background
    dl->AddRectFilled(win_pos, {win_pos.x + PALETTE_W - 1.f, win_pos.y + LAYER_TITLE_H - 1.f},
                      IM_COL32(25, 25, 35, 255));

    // Title text
    ImGui::SetCursorScreenPos({win_pos.x + 4.f, win_pos.y + 4.f});
    ImGui::TextUnformatted("Layers");

    // Add / delete layer buttons (centered vertically in title row)
    const float btn_y = win_pos.y + (static_cast<float>(LAYER_TITLE_H) - LAYER_BTN_H) * 0.5f;

    {
      const ImVec2 btn_min{win_pos.x + LAYER_BTN_ADD_X, btn_y};
      const ImVec2 btn_max{btn_min.x + LAYER_BTN_W, btn_min.y + LAYER_BTN_H};
      dl->AddRectFilled(btn_min, btn_max, IM_COL32(50, 50, 70, 255), 3.f);
      ImGui::SetCursorScreenPos({btn_min.x + 3.f, btn_min.y + 1.f});
      ImGui::TextUnformatted("+");
    }
    {
      const ImVec2 btn_min{win_pos.x + LAYER_BTN_DEL_X, btn_y};
      const ImVec2 btn_max{btn_min.x + LAYER_BTN_W, btn_min.y + LAYER_BTN_H};
      dl->AddRectFilled(btn_min, btn_max, IM_COL32(50, 50, 70, 255), 3.f);
      ImGui::SetCursorScreenPos({btn_min.x + 3.f, btn_min.y + 1.f});
      ImGui::TextUnformatted("-");
    }

    const float text_h = ImGui::GetTextLineHeight();
    const float text_offset_y = (static_cast<float>(LAYER_ROW_H) - text_h) * 0.5f;
    const float icon_x = PALETTE_W - 1.f - text_h - 4.f;

    for (int i = 0; i < n_layers; ++i) {
      const bool active = (i == state.active_layer);
      const bool visible = state.map.layers[static_cast<std::size_t>(i)].visible;
      const float row_top = win_pos.y + static_cast<float>(LAYER_TITLE_H + i * LAYER_ROW_H);

      // Row background
      dl->AddRectFilled({win_pos.x, row_top}, {win_pos.x + PALETTE_W - 1.f, row_top + LAYER_ROW_H - 1.f},
                        active ? IM_COL32(60, 80, 120, 255) : IM_COL32(40, 40, 55, 255));

      // Layer name
      ImGui::SetCursorScreenPos({win_pos.x + 4.f, row_top + text_offset_y});
      ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4{173 / 255.f, 179 / 255.f, 215 / 255.f, 1.f}
                                                  : ImVec4{104 / 255.f, 107 / 255.f, 129 / 255.f, 1.f});
      ImGui::TextUnformatted(state.map.layers[static_cast<std::size_t>(i)].name.c_str());
      ImGui::PopStyleColor();

      // Visibility icon
      ImGui::SetCursorScreenPos({win_pos.x + icon_x, row_top + text_offset_y * 0.1f});
      ImGui::PushStyleColor(ImGuiCol_Text,
                            visible ? GetTextColor(theme, TextRole::Active) : GetTextColor(theme, TextRole::Disabled));
      ImGui::PushFont(fonts.icons);
      ImGui::TextUnformatted(visible ? "\xe2\x97\x8f" : "\xe2\x97\x8b");
      ImGui::PopFont();
      ImGui::PopStyleColor();
    }

    // Advance cursor past the entire strip.
    // Dummy() is required by ImGui after SetCursorScreenPos to commit the new
    // cursor position as the parent window's content boundary.
    const float strip_h = static_cast<float>(LAYER_TITLE_H + n_layers * LAYER_ROW_H);
    ImGui::SetCursorScreenPos({win_pos.x, win_pos.y + strip_h});
    ImGui::Dummy({0.f, 0.f});
  }

} // namespace tools::tilemap
