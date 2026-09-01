#include "render_status_bar.hpp"
#include "layout.hpp"
#include <corundum/world/tilemap/tilemap.hpp>
#include <format>
#include <imgui.h>

namespace tools::tilemap {

  using namespace corundum::tool_host;

  namespace {
    /// Draws one status-bar segment in `role`'s color, preceded by a muted "|" separator (unless
    /// `first` is true, for the very first segment on the line). No-op for an empty `text` so an
    /// inactive conditional segment (e.g. elevation_label when show_elevation is false) doesn't
    /// leave a stray separator behind — the next rendered segment ends up directly after the last
    /// one that actually drew something, with exactly one "|" between them either way.
    void segment(const ThemeColors &theme, TextRole role, const std::string &text, bool first = false) {
      if (text.empty())
        return;
      if (!first) {
        ImGui::SameLine();
        TextColoredRole(theme, TextRole::Muted, "|");
        ImGui::SameLine();
      }
      TextColoredRole(theme, role, text.c_str());
    }
  } // namespace

  void render_status_bar(const EditorState &state, const ThemeColors &theme) {
    // Text (ImGui overlay)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{4.f, 4.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowPos(ImVec2{0.f, static_cast<float>(k_menu_h + CANVAS_H)});
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
    const corundum::world::tilemap::TilemapTileset *ts =
        corundum::world::tilemap::find_tileset(state.map.tilesets, state.selected_gid);
    if (ts) {
      const int local_id = static_cast<int>(state.selected_gid) - static_cast<int>(ts->first_gid);
      const std::string &name = (local_id >= 0 && static_cast<std::size_t>(local_id) < ts->info.tile_names.size())
                                    ? ts->info.tile_names[static_cast<std::size_t>(local_id)]
                                    : std::string{};
      tile_label = name.empty() ? std::format("id: {}", local_id) : std::format("{}", name);
    }

    // Hovered tile GID — shows the actual GID stored at the cursor position on the active layer.
    std::string hover_label;
    const bool hover_valid = state.hover_tile_col >= 0 && state.hover_tile_row >= 0 &&
                             state.hover_tile_col < state.map.width && state.hover_tile_row < state.map.height &&
                             state.active_layer < static_cast<int>(state.map.layers.size());
    if (hover_valid) {
      const auto &layer = state.map.layers[static_cast<std::size_t>(state.active_layer)];
      const auto hover_gid = state.map.layer_view(layer)[state.hover_tile_row, state.hover_tile_col];
      if (hover_gid == corundum::world::tilemap::k_empty_tile) {
        hover_label = std::format("[hover ({},{}): empty]", state.hover_tile_col, state.hover_tile_row);
      } else {
        const corundum::world::tilemap::TilemapTileset *hover_ts =
            corundum::world::tilemap::find_tileset(state.map.tilesets, hover_gid);
        if (hover_ts) {
          const int local_id = static_cast<int>(hover_gid) - static_cast<int>(hover_ts->first_gid);
          const std::string &name =
              (local_id >= 0 && static_cast<std::size_t>(local_id) < hover_ts->info.tile_names.size())
                  ? hover_ts->info.tile_names[static_cast<std::size_t>(local_id)]
                  : std::string{};
          hover_label = std::format("[hover ({},{}): GID {} local_id {} ({})]", state.hover_tile_col,
                                    state.hover_tile_row, hover_gid, local_id, name.empty() ? "?" : name);
        } else {
          hover_label = std::format("[hover ({},{}): GID {} (no tileset)]", state.hover_tile_col, state.hover_tile_row,
                                    hover_gid);
        }
      }
    }

    std::string flip_label;
    if (state.selected_flip == (corundum::world::tilemap::k_flip_h | corundum::world::tilemap::k_flip_v))
      flip_label = "[flip: HV]";
    else if (state.selected_flip == corundum::world::tilemap::k_flip_h)
      flip_label = "[flip: H]";
    else if (state.selected_flip == corundum::world::tilemap::k_flip_v)
      flip_label = "[flip: V]";

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
        elevation_label = std::format("[elev brush:{} hover:{}]", state.selected_elevation, hover_elev);
      } else {
        elevation_label = std::format("[elev brush:{}]", state.selected_elevation);
      }
    }

    const std::string walkability_label =
        state.show_walkability ? std::format("[walkability: max_step {}]", state.max_step_height) : "";

    using corundum::world::tilemap::RampAxis;
    const std::string ramp_label =
        state.show_ramps
            ? std::format("[ramp axis: {}]", state.selected_ramp_axis == RampAxis::NorthSouth ? "N-S" : "E-W")
            : "";

    ImGui::SetCursorPosX(15.0f);
    segment(theme, TextRole::Normal, std::format("[layer: {}]", layer_name), /*first=*/true);
    segment(theme, TextRole::Normal, tile_label.empty() ? "" : std::format("[{}]", tile_label));
    segment(theme, TextRole::Muted, hover_label);
    segment(theme, TextRole::Muted, flip_label);
    segment(theme, TextRole::Accent, elevation_label);
    segment(theme, TextRole::Accent, ramp_label);
    segment(theme, TextRole::Accent, walkability_label);
    segment(theme, state.dirty ? TextRole::Warning : TextRole::Success, state.dirty ? "* unsaved" : "saved");
    segment(theme, TextRole::Muted, "[Cmd+S to save]  [G: grid]  [ESC or Q to quit]");

    ImGui::End();
  }

} // namespace tools::tilemap
