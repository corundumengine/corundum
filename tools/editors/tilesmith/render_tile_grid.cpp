// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "render_tile_grid.hpp"
#include "coords.hpp"
#include "editor_state.hpp"
#include "layout.hpp"
#include "tilemap_rendering.hpp"
#include "tileset_view.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <corundum/core/content_source.hpp>
#include <corundum/toolkit/host/tool_host.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tools::tilesmith {

namespace {

using corundum::world::tilemap::k_empty_tile;
using corundum::world::tilemap::TileId;
using corundum::world::tilemap::TilemapTileset;

constexpr int k_pivot_panel_h = 80;

/// The add and remove sprite-sheet dialogs, which stay open across frames.
struct SheetDialogs {
  std::array<char, 512> add_path{};

  int remove_idx{-1};

  bool show_add{};

  bool show_remove_in_use{};

  bool show_remove_warning{};
};

/// Heights of the palette panel's bands, in screen pixels: the layer strip, then the tab bar,
/// then the tile grid, then the pivot panel at the bottom of the canvas.
struct PaletteBands {
  int layer_strip_h{};

  int tab_h{};

  [[nodiscard]] int grid_top_y() const { return k_menu_h + layer_strip_h + tab_h; }

  [[nodiscard]] int grid_h() const { return CANVAS_H - layer_strip_h - tab_h - k_pivot_panel_h; }
};

void fix_gids_after_removal(EditorState &state, int removed_first_gid, int removed_count) {
  const int first_shifted_gid = removed_first_gid + removed_count;
  const auto shift_down = [&](TileId &gid) {
    if (std::cmp_greater_equal(gid, first_shifted_gid))
      gid = static_cast<TileId>(static_cast<int>(gid) - removed_count);
  };
  for (auto &layer : state.map.layers) {
    for (auto &gid : layer.tiles) {
      if (gid != k_empty_tile)
        shift_down(gid);
    }
    for (const auto &[_, cell] : layer.animated_cells)
      std::ranges::for_each(cell.frame_gids, shift_down);
  }
}

bool tileset_has_any_tile(const EditorState &state, int tileset_idx) {
  if (tileset_idx < 0 || std::cmp_greater_equal(tileset_idx, state.map.tilesets.size()))
    return false;
  const auto &ts = state.map.tilesets[static_cast<std::size_t>(tileset_idx)];
  const TileId range_start = ts.first_gid;
  const auto range_end = static_cast<TileId>(static_cast<int>(ts.first_gid) + ts.info.tile_count);
  const auto in_tileset = [&](TileId gid) { return range_start <= gid && gid < range_end; };
  const auto cell_uses_tileset = [&](const auto &entry) {
    return std::ranges::any_of(entry.second.frame_gids, in_tileset);
  };
  const auto layer_uses_tileset = [&](const corundum::world::tilemap::TilemapLayer &layer) {
    return std::ranges::any_of(layer.tiles, in_tileset) || std::ranges::any_of(layer.animated_cells, cell_uses_tileset);
  };
  return std::ranges::any_of(state.map.layers, layer_uses_tileset);
}

void do_add_tileset(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                    std::vector<TilesetView> &tileset_views, const std::string &source_path) {
  using corundum::world::tilemap::TilesetInfo;

  auto tileset_result = corundum::world::tilemap::load_tileset(corundum::core::DirectoryContentSource{}, source_path);
  if (!tileset_result)
    throw std::runtime_error(tileset_result.error());
  TilesetInfo info = std::move(*tileset_result);

  // Compute first_gid for the new tileset (appended at the end).
  TileId first_gid = 0;
  if (!state.map.tilesets.empty()) {
    const auto &last = state.map.tilesets.back();
    first_gid = static_cast<TileId>(static_cast<int>(last.first_gid) + last.info.tile_count);
  }

  TilemapTileset ts;
  ts.info = std::move(info);
  ts.first_gid = first_gid;

  state.map.tilesets.push_back(std::move(ts));

  auto tex_result = host.textures().load(state.map.tilesets.back().info.path);
  if (!tex_result) {
    state.map.tilesets.pop_back();
    throw std::runtime_error(tex_result.error());
  }
  texture_store.textures.push_back(*tex_result);

  tileset_views = rebuild_tileset_views(host, state.map, texture_store);
  state.palette_tileset_idx = static_cast<int>(tileset_views.size()) - 1;
  state.palette_scroll_y = 0.f;
  state.dirty = true;
}

void do_remove_tileset(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                       std::vector<TilesetView> &tileset_views, int idx) {
  if (idx < 0 || std::cmp_greater_equal(idx, state.map.tilesets.size()))
    return;

  const auto &removed_ts = state.map.tilesets[static_cast<std::size_t>(idx)];
  const int removed_first_gid = removed_ts.first_gid;
  const int removed_count = removed_ts.info.tile_count;

  // Destroy the GPU texture for this tileset
  host.textures().destroy(texture_store.textures[static_cast<std::size_t>(idx)].id);
  texture_store.textures.erase(texture_store.textures.begin() + idx);

  state.map.tilesets.erase(state.map.tilesets.begin() + idx);

  // Shift first_gid for tilesets that came after the removed one.
  for (auto i = static_cast<std::size_t>(idx); i < state.map.tilesets.size(); ++i)
    state.map.tilesets[i].first_gid =
        static_cast<TileId>(static_cast<int>(state.map.tilesets[i].first_gid) - removed_count);

  // Adjust existing tile GIDs so they still point to the correct tiles.
  fix_gids_after_removal(state, removed_first_gid, removed_count);

  tileset_views = rebuild_tileset_views(host, state.map, texture_store);

  if (std::cmp_greater_equal(state.palette_tileset_idx, tileset_views.size()))
    state.palette_tileset_idx = std::max(0, static_cast<int>(tileset_views.size()) - 1);
  state.palette_scroll_y = 0.f;
  state.dirty = true;
}

/// Add the sheet at `typed_path`, accepting a pasted path wrapped in double quotes.
/// @return Whether the sheet was added; a failure is printed to stderr.
bool try_add_tileset(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                     std::vector<TilesetView> &tileset_views, std::string_view typed_path) {
  const bool has_quotes = typed_path.starts_with('"') && typed_path.ends_with('"');
  const bool is_quoted = typed_path.size() >= 2 && has_quotes;
  const std::string path{is_quoted ? typed_path.substr(1, typed_path.size() - 2) : typed_path};
  if (path.empty())
    return false;
  try {
    do_add_tileset(host, state, texture_store, tileset_views, path);
    return true;
  } catch (const std::runtime_error &e) {
    std::println(stderr, "[Tilesmith] Failed to add sprite sheet: {}", e.what());
    return false;
  }
}

/// Display name of the sheet a view shows: its source file's stem.
std::string sheet_display_name(const TilesetView &view) {
  return std::filesystem::path(view.tileset->info.source).stem().string();
}

/// Display name of the sheet at `idx`, or std::nullopt when `idx` names no sheet.
std::optional<std::string> find_sheet_name(const std::vector<TilesetView> &tileset_views, int idx) {
  if (idx < 0 || std::cmp_greater_equal(idx, tileset_views.size()))
    return std::nullopt;
  return sheet_display_name(tileset_views[static_cast<std::size_t>(idx)]);
}

int measure_tab_h() {
  const float frame_h = ImGui::GetFrameHeight();
  const auto &style = ImGui::GetStyle();
  return static_cast<int>(
      std::ceil(frame_h + frame_h + style.TabBarBorderSize + style.ItemSpacing.y + (style.WindowPadding.y * 0.5f)));
}

/// Draw the sprite-sheet tab bar. Selecting a tab shows that sheet in the palette; the "+" tab
/// opens the add dialog.
/// @return The indices of the tabs the user closed this frame.
std::vector<int> render_tileset_tabs(EditorState &state, const std::vector<TilesetView> &tileset_views,
                                     const PaletteBands &bands, SheetDialogs &dialogs) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::SetNextWindowPos(ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(k_menu_h + bands.layer_strip_h)});
  ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(bands.tab_h)});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30.f / 255.f, 30.f / 255.f, 40.f / 255.f, 1.f});
  ImGui::Begin("Sprite Sheets", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  std::vector<int> closed;
  if (ImGui::BeginTabBar("##tilesets")) {
    const int n_tilesets = static_cast<int>(tileset_views.size());
    for (int i = 0; i < n_tilesets; ++i) {
      const std::string label = sheet_display_name(tileset_views[static_cast<std::size_t>(i)]);
      bool keep = true;
      if (ImGui::BeginTabItem(label.c_str(), &keep)) {
        if (state.palette_tileset_idx != i)
          state.palette_scroll_y = 0.f;
        state.palette_tileset_idx = i;
        ImGui::EndTabItem();
      }
      if (!keep)
        closed.push_back(i);
    }

    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder))
      dialogs.show_add = true;

    ImGui::EndTabBar();
  }
  ImGui::End();
  return closed;
}

/// Act on a closed tab: refuse a sheet the map still uses, remove the last sheet at once, and
/// ask before removing any other, because that shifts the GIDs of every later sheet.
void request_tileset_removal(corundum::toolkit::host::ToolHost &host, EditorState &state,
                             TilemapTextureStore &texture_store, std::vector<TilesetView> &tileset_views, int idx,
                             SheetDialogs &dialogs) {
  dialogs.remove_idx = idx;
  if (tileset_has_any_tile(state, idx)) {
    dialogs.show_remove_in_use = true;
    return;
  }
  const bool is_last_sheet = std::cmp_equal(idx + 1, tileset_views.size());
  if (is_last_sheet)
    do_remove_tileset(host, state, texture_store, tileset_views, idx);
  else
    dialogs.show_remove_warning = true;
}

void render_in_use_popup(const std::vector<TilesetView> &tileset_views, SheetDialogs &dialogs) {
  if (dialogs.show_remove_in_use) {
    ImGui::OpenPopup("Cannot Remove Sprite Sheet");
    dialogs.show_remove_in_use = false;
  }
  if (!ImGui::BeginPopupModal("Cannot Remove Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  std::string msg;
  if (const std::optional<std::string> name = find_sheet_name(tileset_views, dialogs.remove_idx))
    msg = std::format("Cannot remove '{}': the sprite sheet is in use by tiles in the map.\n\n"
                      "Erase all tiles using this sheet before removing it.",
                      *name);
  ImGui::TextUnformatted(msg.c_str());
  ImGui::Spacing();
  if (ImGui::Button("OK", ImVec2{120.f, 0.f})) {
    dialogs.remove_idx = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void render_remove_warning_popup(corundum::toolkit::host::ToolHost &host, EditorState &state,
                                 TilemapTextureStore &texture_store, std::vector<TilesetView> &tileset_views,
                                 SheetDialogs &dialogs) {
  if (dialogs.show_remove_warning) {
    ImGui::OpenPopup("Remove Sprite Sheet");
    dialogs.show_remove_warning = false;
  }
  if (!ImGui::BeginPopupModal("Remove Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  std::string msg;
  if (const std::optional<std::string> name = find_sheet_name(tileset_views, dialogs.remove_idx))
    msg = std::format("Remove '{}'?\n\nThis will shift GIDs for all subsequent tilesets.\n"
                      "Existing tiles referencing shifted GIDs will display incorrectly.\n\n"
                      "Remove anyway?",
                      *name);
  ImGui::TextUnformatted(msg.c_str());
  ImGui::Spacing();

  if (ImGui::Button("Remove", ImVec2{120.f, 0.f})) {
    do_remove_tileset(host, state, texture_store, tileset_views, dialogs.remove_idx);
    dialogs.remove_idx = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2{120.f, 0.f})) {
    dialogs.remove_idx = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void render_add_popup(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                      std::vector<TilesetView> &tileset_views, SheetDialogs &dialogs) {
  if (dialogs.show_add) {
    ImGui::OpenPopup("Add Sprite Sheet");
    dialogs.add_path.front() = '\0';
    dialogs.show_add = false;
  }
  if (!ImGui::BeginPopupModal("Add Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextUnformatted("Path to sprite sheet JSON (relative to project root):");
  ImGui::Spacing();
  ImGui::InputText("##source_path", dialogs.add_path.data(), dialogs.add_path.size());
  ImGui::Spacing();

  bool added = false;
  if (ImGui::Button("Add", ImVec2{120.f, 0.f}))
    added = try_add_tileset(host, state, texture_store, tileset_views, dialogs.add_path.data());
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}) || added)
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
}

/// The panel-local y at which to draw `cell` once scrolled by `scroll_y`, or std::nullopt
/// when no part of the cell lies inside the grid's `grid_h`-pixel band.
std::optional<float> visible_cell_y(const PaletteCell &cell, float scroll_y, int grid_h) {
  const float draw_y = static_cast<float>(cell.y) - scroll_y;
  const bool is_above = draw_y + static_cast<float>(cell.h) < 0.f;
  const bool is_below = draw_y > static_cast<float>(grid_h);
  if (is_above || is_below)
    return std::nullopt;
  return draw_y;
}

/// Checkerboard the whole grid window, not just each tile, so transparency reads clearly
/// everywhere, including the empty space around and between variable-sized packed tiles.
void draw_checkerboard(int grid_h) {
  constexpr float k_checker_size = 8.f;
  constexpr ImU32 k_checker_light = IM_COL32(180, 180, 180, 255);
  constexpr ImU32 k_checker_dark = IM_COL32(155, 155, 155, 255);
  const ImVec2 origin = ImGui::GetWindowPos();
  auto *dl = ImGui::GetWindowDrawList();
  const int cols = static_cast<int>(std::ceil(static_cast<float>(PALETTE_W) / k_checker_size));
  const int rows = static_cast<int>(std::ceil(static_cast<float>(grid_h) / k_checker_size));
  for (int cy = 0; cy < rows; ++cy) {
    for (int cx = 0; cx < cols; ++cx) {
      const ImVec2 p_min{origin.x + (static_cast<float>(cx) * k_checker_size),
                         origin.y + (static_cast<float>(cy) * k_checker_size)};
      const ImVec2 p_max{p_min.x + k_checker_size, p_min.y + k_checker_size};
      dl->AddRectFilled(p_min, p_max, ((cx + cy) % 2 == 0) ? k_checker_light : k_checker_dark);
    }
  }
}

void render_tile_images(const EditorState &state, const TilesetView &view, const std::vector<PaletteCell> &cells,
                        const PaletteBands &bands) {
  const auto &ts = *view.tileset;
  const auto tex_w = static_cast<float>(view.tex_w);
  const auto tex_h = static_cast<float>(view.tex_h);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.f, 0.f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::SetNextWindowPos(ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(bands.grid_top_y())});
  ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(bands.grid_h())});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30.f / 255.f, 30.f / 255.f, 40.f / 255.f, 1.f});
  ImGui::Begin("##tilegrid", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoMouseInputs);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  draw_checkerboard(bands.grid_h());

  for (const auto &cell : cells) {
    const std::optional<float> draw_y = visible_cell_y(cell, state.palette_scroll_y, bands.grid_h());
    if (!draw_y)
      continue;
    const auto &rect = ts.info.tile_rects[static_cast<std::size_t>(cell.local_id)];
    const ImVec2 uv0{static_cast<float>(rect.x) / tex_w, static_cast<float>(rect.y) / tex_h};
    const ImVec2 uv1{static_cast<float>(rect.x + rect.width) / tex_w, static_cast<float>(rect.y + rect.height) / tex_h};
    ImGui::SetCursorPos(ImVec2{static_cast<float>(cell.x), *draw_y});
    ImGui::Image(view.tex_id, ImVec2{static_cast<float>(cell.w), static_cast<float>(cell.h)}, uv0, uv1);
  }
  ImGui::End();
}

/// Local id of the palette's selected tile within `ts`, or std::nullopt when the selection
/// belongs to another sheet.
std::optional<std::size_t> selected_local_id(const EditorState &state, const TilemapTileset &ts) {
  const int local_id = static_cast<int>(state.selected_gid) - static_cast<int>(ts.first_gid);
  if (local_id < 0 || local_id >= ts.info.tile_count)
    return std::nullopt;
  return static_cast<std::size_t>(local_id);
}

/// Outline the selected tile on the foreground draw list, on top of the tile images.
void draw_selection_highlight(const EditorState &state, const PaletteCell &cell, const PaletteBands &bands) {
  const std::optional<float> draw_y = visible_cell_y(cell, state.palette_scroll_y, bands.grid_h());
  if (!draw_y)
    return;
  const ImVec2 p_min{static_cast<float>(CANVAS_W + cell.x), static_cast<float>(bands.grid_top_y()) + *draw_y};
  const ImVec2 p_max{p_min.x + static_cast<float>(cell.w), p_min.y + static_cast<float>(cell.h)};
  ImGui::GetForegroundDrawList()->AddRect(p_min, p_max, IM_COL32(255, 200, 0, 255), 0.f, 0, 2.f);
}

/// ImGui::TextDisabled without the printf-style formatting.
void text_disabled(const char *text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
}

/// ImGui::SetTooltip without the printf-style formatting.
void show_tooltip(const char *text) {
  if (!ImGui::BeginTooltip())
    return;
  ImGui::TextUnformatted(text);
  ImGui::EndTooltip();
}

/// Edit the selected tile's own pivot; spritepacker computes one per tile, so there is no
/// tileset-wide default to edit. With no tile selected the sliders show the default pivot and
/// change nothing.
void render_pivot_panel(EditorState &state, TilemapTileset &active_ts, std::optional<std::size_t> selected) {
  float pivot_x = selected ? active_ts.info.tile_pivot_x[*selected] : 0.5f;
  float pivot_y = selected ? active_ts.info.tile_pivot_y[*selected] : 0.f;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.f, 0.f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::SetNextWindowPos(
      ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(k_menu_h + CANVAS_H - k_pivot_panel_h)});
  ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(k_pivot_panel_h)});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{22.f / 255.f, 22.f / 255.f, 32.f / 255.f, 1.f});
  ImGui::Begin("##pivotpanel", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);

  const ImVec2 panel_top_left = ImGui::GetWindowPos();
  ImGui::GetWindowDrawList()->AddLine(panel_top_left, {panel_top_left.x + PALETTE_W, panel_top_left.y},
                                      IM_COL32(80, 80, 100, 255), 1.f);
  ImGui::SetCursorPosY(8.f);
  text_disabled("Origin");
  ImGui::SameLine(60.f);
  ImGui::SetNextItemWidth(88.f);
  const bool x_changed = ImGui::DragFloat("X##pivot_x", &pivot_x, 0.005f, 0.f, 1.f, "%.3f");
  if (ImGui::IsItemHovered())
    show_tooltip("Horizontal pivot\n0 = left edge, 1 = right edge");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(88.f);
  const bool y_changed = ImGui::DragFloat("Y##pivot_y", &pivot_y, 0.005f, 0.f, 1.f, "%.3f");
  if (ImGui::IsItemHovered())
    show_tooltip("Vertical pivot\n0 = bottom edge, 1 = top edge");

  const bool pivot_changed = x_changed || y_changed;
  if (selected.has_value() && pivot_changed) {
    active_ts.info.tile_pivot_x[*selected] = std::clamp(pivot_x, 0.f, 1.f);
    active_ts.info.tile_pivot_y[*selected] = std::clamp(pivot_y, 0.f, 1.f);
    state.dirty = true;
  }

  ImGui::End();
}

} // namespace

void render_tile_grid(corundum::toolkit::host::ToolHost &host, EditorState &state, TilemapTextureStore &texture_store,
                      std::vector<TilesetView> &tileset_views) {
  // TODO(editor-state): the dialogs outlive the frame, so they belong in EditorState rather than in
  // a function-static.
  static SheetDialogs dialogs;

  const int n_layers = static_cast<int>(state.map.layers.size());
  const PaletteBands bands{.layer_strip_h = LAYER_TITLE_H + (n_layers * LAYER_ROW_H), .tab_h = measure_tab_h()};
  state.palette_tabbar_h = bands.tab_h;

  const std::vector<int> closed_tabs = render_tileset_tabs(state, tileset_views, bands, dialogs);
  // TODO(suspect): closing two tabs in one frame removes neither, though an earlier comment said
  // the first one would be handled.
  if (closed_tabs.size() == 1)
    request_tileset_removal(host, state, texture_store, tileset_views, closed_tabs.front(), dialogs);

  render_in_use_popup(tileset_views, dialogs);
  render_remove_warning_popup(host, state, texture_store, tileset_views, dialogs);
  render_add_popup(host, state, texture_store, tileset_views, dialogs);

  if (bands.grid_h() <= 0 || tileset_views.empty())
    return;

  if (std::cmp_greater_equal(state.palette_tileset_idx, tileset_views.size()))
    state.palette_tileset_idx = 0;

  const auto palette_idx = static_cast<std::size_t>(state.palette_tileset_idx);
  const TilesetView &view = tileset_views[palette_idx];
  // palette_tile_scale is user-controlled (Ctrl+scroll, see input.cpp), not auto-fit to any one
  // tile — a packed tileset can mix wildly different native sizes, so there's no single "target"
  // tile size that would display everything else at a readable scale.
  const std::vector<PaletteCell> cells = compute_palette_layout(*view.tileset, PALETTE_W, state.palette_tile_scale);
  render_tile_images(state, view, cells, bands);

  const std::optional<std::size_t> selected = selected_local_id(state, *view.tileset);
  if (selected)
    draw_selection_highlight(state, cells[*selected], bands);

  render_pivot_panel(state, state.map.tilesets[palette_idx], selected);
}

} // namespace tools::tilesmith
