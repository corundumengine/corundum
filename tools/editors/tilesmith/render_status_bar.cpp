#include "render_status_bar.hpp"
#include "layout.hpp"
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <format>
#include <imgui.h>

namespace tools::tilemap {

  void render_status_bar(const EditorState &state) {
    // Text (ImGui overlay)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{4.f, 4.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowPos(ImVec2{0.f, static_cast<float>(CANVAS_H)});
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(WINDOW_W), static_cast<float>(STATUS_H)});
    ImGui::Begin("##statusbar", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoMouseInputs);
    ImGui::PopStyleVar(2);

    const std::string layer_name = state.active_layer < static_cast<int>(state.map.layers.size())
                                       ? state.map.layers[static_cast<std::size_t>(state.active_layer)].name
                                       : "?";

    // Selected palette tile label.
    std::string tile_label;
    const auto *ts = corundum::gameplay::world::tilemap::find_tileset(state.map.tilesets, state.selected_gid);
    if (ts) {
      const int local_id = static_cast<int>(state.selected_gid) - static_cast<int>(ts->first_gid);
      const int col = local_id % ts->info.columns;
      const int row = local_id / ts->info.columns;
      tile_label = std::format("col: {}, row: {}", col, row);
    }

    // Hovered tile GID — shows the actual GID stored at the cursor position on the active layer.
    std::string hover_label;
    const bool hover_valid = state.hover_tile_col >= 0 && state.hover_tile_row >= 0 &&
                             state.hover_tile_col < state.map.width && state.hover_tile_row < state.map.height &&
                             state.active_layer < static_cast<int>(state.map.layers.size());
    if (hover_valid) {
      const auto &layer = state.map.layers[static_cast<std::size_t>(state.active_layer)];
      const auto hover_gid = layer.view(state.map.width, state.map.height)[state.hover_tile_row, state.hover_tile_col];
      if (hover_gid == corundum::gameplay::world::tilemap::k_empty_tile) {
        hover_label = std::format("  [hover ({},{}): empty]", state.hover_tile_col, state.hover_tile_row);
      } else {
        const auto *hover_ts = corundum::gameplay::world::tilemap::find_tileset(state.map.tilesets, hover_gid);
        if (hover_ts) {
          const int local_id = static_cast<int>(hover_gid) - static_cast<int>(hover_ts->first_gid);
          hover_label = std::format("  [hover ({},{}): GID {} local_id {} (sheet col {}, row {})]",
                                    state.hover_tile_col, state.hover_tile_row, hover_gid, local_id,
                                    local_id % hover_ts->info.columns, local_id / hover_ts->info.columns);
        } else {
          hover_label = std::format("  [hover ({},{}): GID {} (no tileset)]", state.hover_tile_col,
                                    state.hover_tile_row, hover_gid);
        }
      }
    }

    std::string flip_label;
    if (state.selected_flip ==
        (corundum::gameplay::world::tilemap::k_flip_h | corundum::gameplay::world::tilemap::k_flip_v))
      flip_label = "  [flip: HV]";
    else if (state.selected_flip == corundum::gameplay::world::tilemap::k_flip_h)
      flip_label = "  [flip: H]";
    else if (state.selected_flip == corundum::gameplay::world::tilemap::k_flip_v)
      flip_label = "  [flip: V]";

    // Elevation readout — reads the active layer's raw value directly (not the
    // cross-layer-resolved elevation_at()), since this should reflect what a
    // paint stroke on the active layer will actually do.
    std::string elevation_label;
    if (state.show_elevation) {
      if (hover_valid) {
        const auto &layer = state.map.layers[static_cast<std::size_t>(state.active_layer)];
        const int hover_elev = layer.elevation.empty()
                                   ? 0
                                   : layer.elevation[static_cast<std::size_t>(state.hover_tile_row * state.map.width +
                                                                              state.hover_tile_col)];
        elevation_label = std::format("  [elev brush:{} hover:{}]", state.selected_elevation, hover_elev);
      } else {
        elevation_label = std::format("  [elev brush:{}]", state.selected_elevation);
      }
    }

    const std::string walkability_label =
        state.show_walkability ? std::format("  [walkability: max_step {}]", state.max_step_height) : "";

    const std::string text = std::format("[layer: {}]{}{}{}{}{}  [Cmd+S to save]{}  [G: grid]  [ESC or Q to quit]",
                                         layer_name, tile_label.empty() ? "" : "  [" + tile_label + "]", hover_label,
                                         flip_label, elevation_label, walkability_label, state.dirty ? "  *" : "");

    ImGui::SetCursorPosX(15.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::End();
  }

} // namespace tools::tilemap
