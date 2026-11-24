// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "render_layer_strip.hpp"
#include "editor_state.hpp"
#include "layout.hpp"
#include <corundum/toolkit/widgets/ui_theme.hpp>
#include <cstddef>
#include <imgui.h>
#include <string>

namespace tools::tilesmith {

using namespace corundum::toolkit::widgets;

namespace {

void center_text_in_button(const ImVec2 &btn_min, const ImVec2 &btn_max, const char *text) {
  const ImVec2 text_size = ImGui::CalcTextSize(text);
  ImGui::SetCursorScreenPos({btn_min.x + ((btn_max.x - btn_min.x - text_size.x) * 0.5f),
                             btn_min.y + ((btn_max.y - btn_min.y - text_size.y) * 0.5f)});
  ImGui::TextUnformatted(text);
}

/** A small filled button with a centred glyph, spanning [btn_x, btn_x + LAYER_BTN_W) on the title row. */
void layer_button(ImDrawList *dl, float btn_x, float btn_y, const char *glyph) {
  const ImVec2 btn_min{btn_x, btn_y};
  const ImVec2 btn_max{btn_min.x + LAYER_BTN_W, btn_min.y + LAYER_BTN_H};
  dl->AddRectFilled(btn_min, btn_max, IM_COL32(50, 50, 70, 255), 3.f);
  center_text_in_button(btn_min, btn_max, glyph);
}

void draw_title_row(ImDrawList *dl, const ImVec2 &win_pos) {
  dl->AddRectFilled(win_pos, {win_pos.x + PALETTE_W - 1.f, win_pos.y + LAYER_TITLE_H - 1.f}, IM_COL32(25, 25, 35, 255));

  ImGui::SetCursorScreenPos({win_pos.x + 4.f, win_pos.y + 4.f});
  ImGui::TextUnformatted("Layers");

  // Add / delete layer buttons, centered vertically in the title row.
  const float btn_y = win_pos.y + ((static_cast<float>(LAYER_TITLE_H) - LAYER_BTN_H) * 0.5f);
  layer_button(dl, win_pos.x + LAYER_BTN_ADD_X, btn_y, "+");
  layer_button(dl, win_pos.x + LAYER_BTN_DEL_X, btn_y, "-");
}

void draw_visibility_icon(ImDrawList *dl, const ThemeColors &theme, const ImVec2 &center, float text_h,
                          bool is_visible) {
  const ImU32 color = ImGui::ColorConvertFloat4ToU32(is_visible ? GetTextColor(theme, TextRole::Active)
                                                                : GetTextColor(theme, TextRole::Disabled));
  const float radius = text_h * 0.35f;
  if (is_visible)
    dl->AddCircleFilled(center, radius, color);
  else
    dl->AddCircle(center, radius, color, 0, 2.f);
}

/** One layer row: background, name (with the depth-sorted marker) and visibility eye. */
void draw_layer_row(ImDrawList *dl, const EditorState &state, const ThemeColors &theme, int index,
                    const ImVec2 &win_pos) {
  const bool active = (index == state.active_layer);
  const float row_top = win_pos.y + static_cast<float>(LAYER_TITLE_H + (index * LAYER_ROW_H));
  const auto layer_index = static_cast<std::size_t>(index);

  dl->AddRectFilled({win_pos.x, row_top}, {win_pos.x + PALETTE_W - 1.f, row_top + LAYER_ROW_H - 1.f},
                    active ? IM_COL32(60, 80, 120, 255) : IM_COL32(40, 40, 55, 255));

  const float text_h = ImGui::GetTextLineHeight();
  const float text_offset_y = (static_cast<float>(LAYER_ROW_H) - text_h) * 0.5f;
  ImGui::SetCursorScreenPos({win_pos.x + 4.f, row_top + text_offset_y});
  ImGui::PushStyleColor(ImGuiCol_Text, active ? ImVec4{173 / 255.f, 179 / 255.f, 215 / 255.f, 1.f}
                                              : ImVec4{104 / 255.f, 107 / 255.f, 129 / 255.f, 1.f});
  std::string label = state.map.layers[layer_index].name;
  if (state.map.layers[layer_index].depth_sorted)
    label += " [S]";
  ImGui::TextUnformatted(label.c_str());
  ImGui::PopStyleColor();

  const float icon_x = PALETTE_W - 1.f - text_h - 4.f;
  const ImVec2 icon_center{win_pos.x + icon_x + (text_h * 0.5f), row_top + (static_cast<float>(LAYER_ROW_H) * 0.5f)};
  draw_visibility_icon(dl, theme, icon_center, text_h, state.layer_visible[layer_index]);
}

} // namespace

void render_layer_strip(const EditorState &state, const ThemeColors &theme) {
  const int n_layers = static_cast<int>(state.map.layers.size());
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 win_pos = ImGui::GetCursorScreenPos();

  draw_title_row(dl, win_pos);
  for (int i = 0; i < n_layers; ++i)
    draw_layer_row(dl, state, theme, i, win_pos);

  // Advance cursor past the entire strip. Dummy() is required by ImGui after SetCursorScreenPos
  // to commit the new cursor position as the parent window's content boundary.
  const float strip_h = static_cast<float>(LAYER_TITLE_H + (n_layers * LAYER_ROW_H));
  ImGui::SetCursorScreenPos({win_pos.x, win_pos.y + strip_h});
  ImGui::Dummy({0.f, 0.f});
}

} // namespace tools::tilesmith
