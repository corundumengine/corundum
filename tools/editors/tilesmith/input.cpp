// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "input.hpp"
#include "coords.hpp"
#include "editor_state.hpp"
#include "elevation_paint.hpp"
#include "fill.hpp"
#include "layer_presets.hpp"
#include "layout.hpp"
#include "menu.hpp"
#include "paint.hpp"
#include "ramp_paint.hpp"
#include "save.hpp"
#include "undo.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <corundum/toolkit/widgets/text_buffer.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <imgui.h>
#include <iterator>
#include <optional>
#include <print>
#include <utility>

namespace tools::tilesmith {

namespace {

/// Convenience overload: fills in the EditorState-derived parameters that are
/// always the same (canvas at k_menu_h origin, CANVAS_W/H, camera/scale from state).
[[nodiscard]] std::optional<TileCoord> editor_screen_to_tile(int px, int py, const EditorState &state) noexcept {
  return screen_to_tile(px, py, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                        state.canvas.scale, state.elev_step_px, state.map.width, state.map.height,
                        effective_diamond_w(state.map), effective_diamond_h(state.map), state.map, state.layer_visible);
}

void begin_collision_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc)
    return;
  state.collision_dragging = true;
  state.col_drag_sub_tile = ImGui::GetIO().KeyShift;
  state.col_drag_anchor_col = tc->col;
  state.col_drag_anchor_row = tc->row;
  state.col_drag_cur_col = tc->col;
  state.col_drag_cur_row = tc->row;
  state.col_drag_anchor_win_x = win_x;
  state.col_drag_anchor_win_y = win_y;
  state.col_drag_cur_win_x = win_x;
  state.col_drag_cur_win_y = win_y;
}

void update_collision_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (!state.collision_dragging || state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (tc) {
    state.col_drag_cur_col = tc->col;
    state.col_drag_cur_row = tc->row;
  }
  state.col_drag_cur_win_x = win_x;
  state.col_drag_cur_win_y = win_y;
}

void commit_collision_rect(EditorState &state) {
  if (!state.collision_dragging || state.map.tilesets.empty())
    return;
  const int tw = state.map.diamond_w();
  const int th = state.map.diamond_h();
  corundum::world::tilemap::CollisionRect candidate;
  if (state.col_drag_sub_tile) {
    candidate = pixel_to_tiled_rect(state.col_drag_anchor_win_x, state.col_drag_anchor_win_y, state.col_drag_cur_win_x,
                                    state.col_drag_cur_win_y, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x,
                                    state.canvas.offset_y, state.canvas.scale, state.elev_step_px, state.map.height, tw,
                                    th, state.map, state.layer_visible);
  } else {
    candidate = snap_to_tile_rect(state.col_drag_anchor_col, state.col_drag_anchor_row, state.col_drag_cur_col,
                                  state.col_drag_cur_row);
  }
  const auto &cols = state.map.collisions;
  bool overlaps = false;
  for (std::size_t ci = 0; ci < cols.size() && !overlaps; ++ci) {
    overlaps = candidate.col < cols.cols[ci] + cols.col_spans[ci] &&
               candidate.col + candidate.col_span > cols.cols[ci] &&
               candidate.row < cols.rows[ci] + cols.row_spans[ci] && candidate.row + candidate.row_span > cols.rows[ci];
  }
  state.collision_dragging = false;
  if (overlaps)
    return;
  // std::floor (not truncate) so a sub-tile rect's col/row identifies the cell
  // containing its top-left corner — matches picking/chunk_at_iso conventions.
  const uint8_t elev = static_cast<uint8_t>(corundum::world::tilemap::elevation_at(
      state.map, static_cast<int>(std::floor(candidate.col)), static_cast<int>(std::floor(candidate.row))));
  state.map.collisions.push_back(candidate.col, candidate.row, candidate.col_span, candidate.row_span, elev);
  state.dirty = true;
  push_undo_checkpoint(state);
}

void place_triangle_at(EditorState &state, int win_x, int win_y) {
  if (state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc)
    return;
  const float col = static_cast<float>(tc->col);
  const float row = static_cast<float>(tc->row);
  constexpr float k_col_span = 1.f;
  constexpr float k_row_span = 1.f;
  auto &tris = state.map.collision_triangles;
  for (std::size_t i = 0; i < tris.size(); ++i) {
    if (tris.cols[i] == col && tris.rows[i] == row && tris.col_spans[i] == k_col_span &&
        tris.row_spans[i] == k_row_span) {
      // Exact cell match: same cut is a no-op; different cut updates in place rather than
      // stacking a second triangle (compare with the half-open `>= && <` test in
      // remove_triangle_at — that one checks "click is anywhere inside the cell").
      if (tris.cuts[i] != state.collision_tri_cut) {
        tris.cuts[i] = state.collision_tri_cut;
        state.dirty = true;
        push_undo_checkpoint(state);
      }
      return;
    }
  }
  const uint8_t elev = static_cast<uint8_t>(corundum::world::tilemap::elevation_at(state.map, tc->col, tc->row));
  tris.push_back(col, row, k_col_span, k_row_span, state.collision_tri_cut, elev);
  state.dirty = true;
  push_undo_checkpoint(state);
}

[[nodiscard]] bool remove_triangle_at(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return false;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc)
    return false;
  const float world_x = static_cast<float>(tc->col);
  const float world_y = static_cast<float>(tc->row);
  auto &tris = state.map.collision_triangles;
  bool removed_any = false;
  std::size_t i = 0;
  while (i < tris.size()) {
    if (world_x >= tris.cols[i] && world_x < tris.cols[i] + tris.col_spans[i] && world_y >= tris.rows[i] &&
        world_y < tris.rows[i] + tris.row_spans[i]) {
      tris.erase(i);
      removed_any = true;
    } else {
      ++i;
    }
  }
  if (removed_any) {
    state.dirty = true;
    push_undo_checkpoint(state);
  }
  return removed_any;
}

void begin_erase_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc)
    return;
  state.erase_dragging = true;
  state.erase_drag_anchor_col = tc->col;
  state.erase_drag_anchor_row = tc->row;
  state.erase_drag_cur_col = tc->col;
  state.erase_drag_cur_row = tc->row;
}

void update_erase_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (!state.erase_dragging || state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (tc) {
    state.erase_drag_cur_col = tc->col;
    state.erase_drag_cur_row = tc->row;
  }
}

void commit_erase_rect(EditorState &state) noexcept {
  if (!state.erase_dragging)
    return;
  const int col_min = std::min(state.erase_drag_anchor_col, state.erase_drag_cur_col);
  const int col_max = std::max(state.erase_drag_anchor_col, state.erase_drag_cur_col);
  const int row_min = std::min(state.erase_drag_anchor_row, state.erase_drag_cur_row);
  const int row_max = std::max(state.erase_drag_anchor_row, state.erase_drag_cur_row);
  erase_rect(state, col_min, row_min, col_max, row_max);
  state.erase_dragging = false;
  state.dirty = true;
  push_undo_checkpoint(state);
}

void begin_portal_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc)
    return;
  state.portal_dragging = true;
  state.portal_drag_anchor_col = tc->col;
  state.portal_drag_anchor_row = tc->row;
  state.portal_drag_cur_col = tc->col;
  state.portal_drag_cur_row = tc->row;
}

void update_portal_drag(EditorState &state, int win_x, int win_y) noexcept {
  if (!state.portal_dragging || state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (tc) {
    state.portal_drag_cur_col = tc->col;
    state.portal_drag_cur_row = tc->row;
  }
}

void commit_portal_rect(EditorState &state) {
  if (!state.portal_dragging || state.map.tilesets.empty())
    return;
  state.portal_dragging = false;
  const int col = std::min(state.portal_drag_anchor_col, state.portal_drag_cur_col);
  const int row = std::min(state.portal_drag_anchor_row, state.portal_drag_cur_row);
  const int w = std::abs(state.portal_drag_cur_col - state.portal_drag_anchor_col) + 1;
  const int h = std::abs(state.portal_drag_cur_row - state.portal_drag_anchor_row) + 1;
  state.portals.push_back(corundum::world::Portal{
      .col = static_cast<float>(col),
      .row = static_cast<float>(row),
      .w = static_cast<float>(w),
      .h = static_cast<float>(h),
  });
  state.selected_portal = static_cast<int>(state.portals.size()) - 1;
  state.dirty = true;
  push_undo_checkpoint(state);
}

void select_portal_at(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return;
  const auto tc = editor_screen_to_tile(win_x, win_y, state);
  if (!tc) {
    state.selected_portal = -1;
    return;
  }

  state.show_portals_popup = true;

  for (int i = static_cast<int>(state.portals.size()) - 1; i >= 0; --i) {
    const auto &p = state.portals[static_cast<std::size_t>(i)];
    const float col = static_cast<float>(tc->col);
    const float row = static_cast<float>(tc->row);
    if (col >= p.col && col < p.col + p.w && row >= p.row && row < p.row + p.h) {
      state.selected_portal = i;
      return;
    }
  }
  state.selected_portal = -1;
}

void remove_collision_at(EditorState &state, int win_x, int win_y) noexcept {
  if (state.map.tilesets.empty())
    return;
  // Right-click outside the canvas is a no-op (matches the old editor_screen_to_tile-based
  // guard, so right-clicks in the palette panel don't accidentally delete rects).
  if (win_x < 0 || win_x >= CANVAS_W || win_y < k_menu_h || win_y >= k_menu_h + CANVAS_H)
    return;
  // Use the same fractional isometric projection as placement so sub-tile rects (Shift-drag)
  // are hit-testable — integer floor would miss any rect whose col/row is non-integral.
  const auto frac = pixel_to_fractional_tile(win_x, win_y, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x,
                                             state.canvas.offset_y, state.canvas.scale, state.elev_step_px,
                                             state.map.height, effective_diamond_w(state.map),
                                             effective_diamond_h(state.map), state.map, state.layer_visible);
  auto &cols = state.map.collisions;
  for (int i = static_cast<int>(cols.size()) - 1; i >= 0; --i) {
    const auto idx = static_cast<std::size_t>(i);
    if (frac.col >= cols.cols[idx] && frac.col < cols.cols[idx] + cols.col_spans[idx] && frac.row >= cols.rows[idx] &&
        frac.row < cols.rows[idx] + cols.row_spans[idx]) {
      cols.erase(idx);
      state.dirty = true;
      push_undo_checkpoint(state);
      return;
    }
  }
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables): TU-local editor UI state.
bool layer_rename_requested = false;      ///< True when user double-clicked a layer.
int layer_rename_idx = -1;                ///< Index of the layer being edited.
std::array<char, 256> layer_rename_buf{}; ///< Buffer for the layer name.
int layer_props_z_index = 0;              ///< Buffer for the layer's z_index, edited in the popup.
bool layer_props_depth_sorted = false;    ///< Buffer for the layer's depth_sorted flag.
bool layer_add_popup_requested = false;   ///< True when the "+" button was clicked.

bool show_fill_blocked_popup = false; ///< True when F was pressed on a non-ground layer.
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void add_layer(EditorState &state, LayerPreset preset) {
  state.map.layers.push_back(make_layer_from_preset(preset, state.map.width, state.map.height, state.map.layers));
  state.layer_visible.push_back(true);
  state.dirty = true;
  push_undo_checkpoint(state);
}

void delete_layer(EditorState &state) noexcept {
  if (state.map.layers.size() <= 1)
    return;
  const int idx = state.active_layer;
  state.map.layers.erase(state.map.layers.begin() + idx);
  state.layer_visible.erase(state.layer_visible.begin() + idx);
  if (std::cmp_greater_equal(state.active_layer, state.map.layers.size()))
    state.active_layer = static_cast<int>(state.map.layers.size()) - 1;
  state.dirty = true;
  push_undo_checkpoint(state);
}

/// The add/delete buttons sharing the layer panel's title row.
void handle_layer_title_click(EditorState &state, int panel_x, int panel_y) {
  const float btn_y = (static_cast<float>(LAYER_TITLE_H) - LAYER_BTN_H) * 0.5f;
  const float panel_x_f = static_cast<float>(panel_x);
  const float panel_y_f = static_cast<float>(panel_y);
  if (panel_y_f < btn_y || panel_y_f >= btn_y + LAYER_BTN_H)
    return;
  if (panel_x_f >= LAYER_BTN_ADD_X && panel_x_f < LAYER_BTN_ADD_X + LAYER_BTN_W) {
    layer_add_popup_requested = true;
    return;
  }
  if (panel_x_f >= LAYER_BTN_DEL_X && panel_x_f < LAYER_BTN_DEL_X + LAYER_BTN_W)
    delete_layer(state);
}

/// A click on one layer row: double-click renames, the eye toggles visibility, else select.
void handle_layer_row_click(EditorState &state, int panel_x, int panel_y, int n_layers) {
  const int idx = (panel_y - LAYER_TITLE_H) / LAYER_ROW_H;
  if (idx < 0 || idx >= n_layers)
    return;
  const auto slot = static_cast<std::size_t>(idx);
  const bool rename = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && panel_x < PALETTE_W - 24;
  if (rename) {
    layer_rename_requested = true;
    layer_rename_idx = idx;
    corundum::toolkit::widgets::copy_to_buffer(layer_rename_buf.data(), layer_rename_buf.size(),
                                               state.map.layers[slot].name);
    layer_props_z_index = state.map.layers[slot].z_index;
    layer_props_depth_sorted = state.map.layers[slot].depth_sorted;
    return;
  }
  if (panel_x >= PALETTE_W - 24) {
    const auto layer_index = static_cast<std::size_t>(idx);
    state.layer_visible[layer_index] = !state.layer_visible[layer_index];
    push_undo_checkpoint(state);
    return;
  }
  state.active_layer = idx;
}

void handle_palette_click(EditorState &state, int win_x, int win_y) {
  const int panel_x = win_x - CANVAS_W;
  const int panel_y = win_y - k_menu_h;

  const int n_layers = static_cast<int>(state.map.layers.size());
  const int layer_strip_h = LAYER_TITLE_H + (n_layers * LAYER_ROW_H);
  if (panel_y < LAYER_TITLE_H) {
    handle_layer_title_click(state, panel_x, panel_y);
    return;
  }
  if (panel_y < layer_strip_h) {
    handle_layer_row_click(state, panel_x, panel_y, n_layers);
    return;
  }

  if (state.map.tilesets.empty())
    return;
  const int grid_x = panel_x;
  const int grid_y = panel_y - layer_strip_h - state.palette_tabbar_h;
  if (grid_y < 0)
    return;
  const auto &ts = state.map.tilesets[static_cast<std::size_t>(state.palette_tileset_idx)];
  const auto layout = compute_palette_layout(ts, PALETTE_W, state.palette_tile_scale);
  const auto gid = palette_click_to_gid(grid_x, grid_y, ts, state.palette_scroll_y, layout);
  if (gid)
    state.selected_gid = *gid;
}

/// Nudge the camera by (`dx`, `dy`) canvas pixels, then keep it inside the map bounds.
void nudge_camera(EditorState &state, float dx, float dy) noexcept {
  state.canvas.offset_x += dx;
  state.canvas.offset_y += dy;
  auto [cx, cy] =
      clamp_camera(state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width, state.map.height,
                   effective_diamond_w(state.map), effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
  state.canvas.offset_x = cx;
  state.canvas.offset_y = cy;
}

/// Step the collision triangle cut `delta` places around the four orientations, wrapping.
void cycle_triangle_cut(EditorState &state, int delta) noexcept {
  using corundum::world::tilemap::TriangleCut;
  constexpr std::array<TriangleCut, 4> k_cuts{
      TriangleCut::NorthWest,
      TriangleCut::NorthEast,
      TriangleCut::SouthEast,
      TriangleCut::SouthWest,
  };
  // std::array's iterator is a class in MSVC's STL, so name the offset rather than the iterator type.
  const std::ptrdiff_t count = std::ssize(k_cuts);
  const std::ptrdiff_t index = std::ranges::find(k_cuts, state.collision_tri_cut) - k_cuts.begin();
  state.collision_tri_cut = k_cuts[static_cast<std::size_t>(((index + delta) + count) % count)];
}

/// Arrow keys pan the camera by one tile along the pressed axis.
void handle_camera_keys(EditorState &state) {
  if (state.map.tilesets.empty())
    return;
  const float diamond_w = static_cast<float>(effective_diamond_w(state.map));
  const float step_px = diamond_w * state.canvas.scale * 0.5f;
  const float step_py = diamond_w * state.canvas.scale * 0.25f;
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    nudge_camera(state, -step_px, 0.f);
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    nudge_camera(state, step_px, 0.f);
  if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    nudge_camera(state, 0.f, -step_py);
  if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    nudge_camera(state, 0.f, step_py);
}

/// G, C and T toggle the grid and the collision overlay, and the collision mode with it.
void handle_overlay_keys(EditorState &state) {
  if (ImGui::IsKeyPressed(ImGuiKey_G))
    state.show_grid = !state.show_grid;

  if (ImGui::IsKeyPressed(ImGuiKey_C)) {
    state.show_collisions = !state.show_collisions;
    if (!state.show_collisions)
      state.triangle_collision_mode = false;
  }

  if (ImGui::IsKeyPressed(ImGuiKey_T) && state.show_collisions)
    state.triangle_collision_mode = !state.triangle_collision_mode;
}

/// P/E/R/W toggle the portal, elevation, ramp and walkability overlays.
void handle_panel_overlay_keys(EditorState &state) {
  if (ImGui::IsKeyPressed(ImGuiKey_P))
    state.show_portals = !state.show_portals;

  if (ImGui::IsKeyPressed(ImGuiKey_E))
    state.show_elevation = !state.show_elevation;

  if (ImGui::IsKeyPressed(ImGuiKey_R))
    state.show_ramps = !state.show_ramps;

  if (ImGui::IsKeyPressed(ImGuiKey_W))
    state.show_walkability = !state.show_walkability;
}

/// The bracket keys step the collision triangle's orientation.
void handle_triangle_cut_keys(EditorState &state) {
  if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
    cycle_triangle_cut(state, 1);
  if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
    cycle_triangle_cut(state, -1);
}

/// The bracket keys step the elevation brush by 1, or by 10 with Shift held.
void handle_elevation_brush_keys(EditorState &state, const ImGuiIO &io) {
  const int step = io.KeyShift ? 10 : 1;
  if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
    state.selected_elevation = static_cast<uint8_t>(std::min(100, static_cast<int>(state.selected_elevation) + step));
  if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
    state.selected_elevation = static_cast<uint8_t>(std::max(0, static_cast<int>(state.selected_elevation) - step));
}

/// Either bracket key flips the ramp brush's axis.
void handle_ramp_brush_keys(EditorState &state) {
  const bool bracket_pressed = ImGui::IsKeyPressed(ImGuiKey_RightBracket) || ImGui::IsKeyPressed(ImGuiKey_LeftBracket);
  if (!bracket_pressed)
    return;
  using corundum::world::tilemap::RampAxis;
  state.selected_ramp_axis =
      state.selected_ramp_axis == RampAxis::NorthSouth ? RampAxis::EastWest : RampAxis::NorthSouth;
}

/// The bracket keys step whichever brush value the open overlay authors.
void handle_brush_keys(EditorState &state, const ImGuiIO &io) {
  if (state.show_collisions && state.triangle_collision_mode) {
    handle_triangle_cut_keys(state);
    return;
  }
  if (state.show_elevation) {
    handle_elevation_brush_keys(state, io);
    return;
  }
  if (state.show_ramps)
    handle_ramp_brush_keys(state);
}

/// X/Y flip the brush, Tab and 1-9 pick the active layer, Delete drops the selected portal.
void handle_selection_keys(EditorState &state) {
  const bool delete_pressed = ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace);
  if (delete_pressed && state.show_portals && state.selected_portal >= 0 &&
      std::cmp_less(state.selected_portal, state.portals.size())) {
    state.portals.erase(state.portals.begin() + state.selected_portal);
    state.selected_portal = -1;
    state.dirty = true;
    push_undo_checkpoint(state);
  }

  if (ImGui::IsKeyPressed(ImGuiKey_X))
    state.selected_flip ^= corundum::world::tilemap::k_flip_h;

  if (ImGui::IsKeyPressed(ImGuiKey_Y))
    state.selected_flip ^= corundum::world::tilemap::k_flip_v;

  if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !state.map.layers.empty())
    state.active_layer = (state.active_layer + 1) % static_cast<int>(state.map.layers.size());

  constexpr std::array<ImGuiKey, 9> k_num_keys{
      ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
  };
  for (int i = 0; i < 9; ++i) {
    if (ImGui::IsKeyPressed(k_num_keys[static_cast<std::size_t>(i)]) && std::cmp_less(i, state.map.layers.size())) {
      state.active_layer = i;
      break;
    }
  }
}

/// F fills the ground layer, Ctrl+Z/Ctrl+Shift+Z walk the undo history.
void handle_edit_keys(EditorState &state, const ImGuiIO &io) {
  if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl &&
      !fill_ground_layer(state, state.selected_gid, state.selected_flip))
    show_fill_blocked_popup = true;

  if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
    apply_redo(state);
  else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    apply_undo(state);
}

/// Keyboard shortcuts: quit, camera pan, overlay toggles, brushes, undo and save.
void handle_keyboard_input(EditorState &state, const ImGuiIO &io, bool &running) {
  if (io.WantCaptureKeyboard)
    return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (ImGui::IsKeyPressed(ImGuiKey_Q) && !io.KeyCtrl)) {
    if (state.dirty)
      request_exit_confirm(state);
    else
      running = false;
    return;
  }

  handle_camera_keys(state);
  handle_overlay_keys(state);
  handle_brush_keys(state, io);
  handle_panel_overlay_keys(state);
  handle_edit_keys(state, io);
  handle_selection_keys(state);

  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    try_save(state);
}

/// Where the cursor is this frame, and which of the two panels it is over.
struct Pointer {
  int mx{};

  int my{};

  bool over_canvas{};

  bool over_panel{};
};

/// The cursor position and panel hit-test for the current frame.
[[nodiscard]] Pointer read_pointer() {
  const ImGuiIO &io = ImGui::GetIO();
  const int mx = static_cast<int>(io.MousePos.x);
  const int my = static_cast<int>(io.MousePos.y);
  return {
      .mx = mx,
      .my = my,
      .over_canvas = mx >= 0 && mx < CANVAS_W && my >= k_menu_h && my < k_menu_h + CANVAS_H,
      .over_panel = mx >= CANVAS_W && my >= k_menu_h && my < k_menu_h + CANVAS_H,
  };
}

/// A left press starts whatever the open overlay paints with, or picks a palette tile.
void handle_left_press(EditorState &state, const Pointer &pointer) {
  if (state.show_portals) {
    if (pointer.over_canvas)
      select_portal_at(state, pointer.mx, pointer.my);
    return;
  }
  if (!pointer.over_canvas) {
    const bool no_brush_overlay = !state.show_collisions && !state.show_elevation && !state.show_ramps;
    if (pointer.over_panel && no_brush_overlay)
      handle_palette_click(state, pointer.mx, pointer.my);
    return;
  }
  if (state.show_collisions) {
    if (state.triangle_collision_mode)
      place_triangle_at(state, pointer.mx, pointer.my);
    else
      begin_collision_drag(state, pointer.mx, pointer.my);
  } else if (state.show_elevation) {
    paint_or_erase_elevation(state, pointer.mx, pointer.my, false);
    state.painting_active = true;
  } else if (state.show_ramps) {
    paint_or_erase_ramp(state, pointer.mx, pointer.my, false);
    state.painting_active = true;
  } else {
    paint_or_erase(state, pointer.mx, pointer.my, false);
    state.painting_active = true;
  }
}

/// A right press erases, or starts the drag that erases when released.
void handle_right_press(EditorState &state, const Pointer &pointer) {
  if (!pointer.over_canvas)
    return;
  if (state.show_portals) {
    begin_portal_drag(state, pointer.mx, pointer.my);
  } else if (state.show_collisions) {
    if (!remove_triangle_at(state, pointer.mx, pointer.my))
      remove_collision_at(state, pointer.mx, pointer.my);
  } else if (state.show_elevation) {
    paint_or_erase_elevation(state, pointer.mx, pointer.my, true);
  } else if (state.show_ramps) {
    paint_or_erase_ramp(state, pointer.mx, pointer.my, true);
  } else {
    begin_erase_drag(state, pointer.mx, pointer.my);
  }
}

/// Left and right button presses over the canvas or the palette.
void handle_mouse_press(EditorState &state, MouseState &mouse, const Pointer &pointer) {
  const bool popup_or_modal_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    mouse.left_held = true;
    if (!popup_or_modal_open && !ImGui::IsAnyItemActive())
      handle_left_press(state, pointer);
  }
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !popup_or_modal_open) {
    mouse.right_held = true;
    handle_right_press(state, pointer);
  }
}

/// Button releases: commit whichever drag or paint pass was running.
void handle_mouse_release(EditorState &state, MouseState &mouse) {
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    mouse.left_held = false;
    const bool was_painting = state.painting_active;
    state.painting_active = false;
    if (!state.show_portals && state.show_collisions && !state.triangle_collision_mode && state.collision_dragging)
      commit_collision_rect(state);
    else if (was_painting)
      push_undo_checkpoint(state);
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
    mouse.right_held = false;
    if (state.show_portals && state.portal_dragging)
      commit_portal_rect(state);
    else if (state.erase_dragging)
      commit_erase_rect(state);
    else if (state.show_elevation || state.show_ramps)
      push_undo_checkpoint(state);
  }
}

/// Track the cell under the cursor for the overlays and the status bar.
void update_hovered_cell(EditorState &state, const Pointer &pointer) {
  const auto tc = editor_screen_to_tile(pointer.mx, pointer.my, state);
  state.hover_tile_col = tc ? tc->col : -1;
  state.hover_tile_row = tc ? tc->row : -1;
}

/// Which overlay's paint brush a drag feeds.
enum class Brush : std::uint8_t { Elevation, Ramp, Tile };

/// The open overlay's continuous paint pass: a held left button paints, a held right button erases.
void update_paint_pass(EditorState &state, const MouseState &mouse, const Pointer &pointer, Brush brush) {
  const bool popup_or_modal_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
  const bool painting = state.painting_active && pointer.over_canvas && !popup_or_modal_open;
  const bool erasing = mouse.right_held && pointer.over_canvas && !popup_or_modal_open;
  switch (brush) {
    case Brush::Elevation:
      if (painting)
        paint_or_erase_elevation(state, pointer.mx, pointer.my, false);
      if (erasing)
        paint_or_erase_elevation(state, pointer.mx, pointer.my, true);
      return;
    case Brush::Ramp:
      if (painting)
        paint_or_erase_ramp(state, pointer.mx, pointer.my, false);
      if (erasing)
        paint_or_erase_ramp(state, pointer.mx, pointer.my, true);
      return;
    case Brush::Tile:
      if (painting)
        paint_or_erase(state, pointer.mx, pointer.my, false);
      if (erasing)
        update_erase_drag(state, pointer.mx, pointer.my);
      return;
  }
}

/// Keep the open overlay's drag or paint pass following the cursor.
void update_active_drag(EditorState &state, const MouseState &mouse, const Pointer &pointer) {
  if (state.show_portals) {
    if (state.portal_dragging)
      update_portal_drag(state, pointer.mx, pointer.my);
  } else if (state.show_collisions) {
    if (state.collision_dragging)
      update_collision_drag(state, pointer.mx, pointer.my);
  } else if (state.show_elevation) {
    update_paint_pass(state, mouse, pointer, Brush::Elevation);
  } else if (state.show_ramps) {
    update_paint_pass(state, mouse, pointer, Brush::Ramp);
  } else {
    update_paint_pass(state, mouse, pointer, Brush::Tile);
  }
}

/// Ctrl+scroll zooms the palette, plain scroll pans it.
void handle_palette_wheel(EditorState &state, const ImGuiIO &io, bool over_panel) {
  // palette_tile_scale is user-controlled, not auto-fit (see render_tile_grid.cpp), and the palette's
  // flow layout has no columns, so a plain wheel scroll can only pan vertically.
  const bool wheel_over_palette = over_panel && io.MouseWheel != 0.f && !state.map.tilesets.empty();
  if (wheel_over_palette && io.KeyCtrl) {
    constexpr float k_zoom_step = 0.1f;
    state.palette_tile_scale =
        std::clamp(state.palette_tile_scale + (io.MouseWheel * k_zoom_step), k_palette_min_scale, k_palette_max_scale);
  } else if (wheel_over_palette) {
    const auto &ts = state.map.tilesets[static_cast<std::size_t>(state.palette_tileset_idx)];
    const auto layout = compute_palette_layout(ts, PALETTE_W, state.palette_tile_scale);
    int content_h = 0;
    for (const auto &cell : layout)
      content_h = std::max(content_h, cell.y + cell.h);
    constexpr float k_wheel_scroll_px = 60.f;
    state.palette_scroll_y -= io.MouseWheel * k_wheel_scroll_px;
    state.palette_scroll_y = std::clamp(state.palette_scroll_y, 0.f, static_cast<float>(std::max(0, content_h)));
  }
}

/// The "Add Layer" preset picker.
void show_add_layer_popup(EditorState &state) {
  if (layer_add_popup_requested) {
    ImGui::OpenPopup("Add Layer");
    layer_add_popup_requested = false;
  }

  if (!ImGui::BeginPopupModal("Add Layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  constexpr std::array<LayerPreset, 7> k_presets{
      LayerPreset::Ground, LayerPreset::FloorDetail, LayerPreset::Water, LayerPreset::Walls,
      LayerPreset::Roof,   LayerPreset::Decor,       LayerPreset::Blank,
  };
  for (const LayerPreset preset : k_presets) {
    if (ImGui::Selectable(std::string(layer_preset_label(preset)).c_str())) {
      add_layer(state, preset);
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::Separator();
  if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

/// The rename picker, which also edits the layer's z-index and depth sorting.
void show_rename_layer_popup(EditorState &state) {
  if (layer_rename_requested) {
    ImGui::OpenPopup("Rename Layer");
    layer_rename_requested = false;
  }

  if (!ImGui::BeginPopupModal("Rename Layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted("Layer name:");
  ImGui::InputText("##rename", layer_rename_buf.data(), layer_rename_buf.size());

  ImGui::TextUnformatted("Z-index:");
  ImGui::InputInt("##zindex", &layer_props_z_index);
  layer_props_z_index = std::max(layer_props_z_index, 0);

  ImGui::BeginDisabled(layer_props_z_index == 0);
  ImGui::Checkbox("Depth-sort with entities", &layer_props_depth_sorted);
  ImGui::EndDisabled();
  if (layer_props_z_index == 0)
    layer_props_depth_sorted = false;

  bool committed = false;
  if (ImGui::Button("Ok", ImVec2{120.f, 0.f})) {
    if (layer_rename_buf[0] != '\0') {
      auto &layer = state.map.layers[static_cast<std::size_t>(layer_rename_idx)];
      layer.name = layer_rename_buf.data();
      layer.z_index = layer_props_z_index;
      layer.depth_sorted = layer_props_depth_sorted;
      state.dirty = true;
      push_undo_checkpoint(state);
    }
    committed = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}))
    committed = true;
  if (committed) {
    layer_rename_idx = -1;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

/// The save-time validation report, which still lets the author save anyway.
void show_validation_popup(EditorState &state) {
  if (state.show_validation_popup) {
    ImGui::OpenPopup("Validation Errors");
    state.show_validation_popup = false;
  }

  if (!ImGui::BeginPopupModal("Validation Errors", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): ImGui's printf-style API is varargs.
  ImGui::TextColored(ImVec4{1.f, .6f, 0.f, 1.f}, "%zu problem(s) found in this map:", state.validation_errors.size());
  ImGui::Spacing();
  for (const auto &msg : state.validation_errors) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): ImGui's printf-style API is varargs.
    ImGui::BulletText("%s", msg.c_str());
  }
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Save Anyway", ImVec2{150.f, 0.f})) {
    if (auto saved = save_tilemap(state); saved)
      std::println("[Tilesmith] Saved with {} validation warning(s): {}", state.validation_errors.size(),
                   state.map_path.string());
    else
      std::println(stderr, "[Tilesmith] Save failed: {}", saved.error());
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

/// The notice shown when F is pressed on a layer fill cannot touch.
void show_fill_blocked_notice() {
  if (show_fill_blocked_popup) {
    ImGui::OpenPopup("Fill Not Available");
    show_fill_blocked_popup = false;
  }

  if (!ImGui::BeginPopupModal("Fill Not Available", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::TextUnformatted("Fill only works on the ground layer (z_index 0).");
  ImGui::Spacing();
  if (ImGui::Button("OK", ImVec2{120.f, 0.f}))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

/// The layer add/rename, validation and fill popups.
void handle_input_popups(EditorState &state) {
  show_add_layer_popup(state);
  show_rename_layer_popup(state);
  show_validation_popup(state);
  show_fill_blocked_notice();
}

} // namespace

void handle_input(EditorState &state, MouseState &mouse, bool &running) {
  const ImGuiIO &io = ImGui::GetIO();
  const Pointer pointer = read_pointer();

  // --- Canvas pan/zoom ---
  state.canvas.update(io, {0.f, static_cast<float>(k_menu_h)},
                      {static_cast<float>(CANVAS_W), static_cast<float>(CANVAS_H)},
                      /*zoom_to_cursor=*/true);

  if (!io.WantCaptureKeyboard)
    handle_keyboard_input(state, io, running);

  handle_mouse_press(state, mouse, pointer);
  handle_mouse_release(state, mouse);
  update_hovered_cell(state, pointer);
  update_active_drag(state, mouse, pointer);
  handle_palette_wheel(state, io, pointer.over_panel);
  handle_input_popups(state);
}

} // namespace tools::tilesmith
