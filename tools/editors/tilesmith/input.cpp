#include "input.hpp"
#include "coords.hpp"
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
#include <cstring>
#include <format>
#include <imgui.h>
#include <print>

namespace tools::tilesmith {

  namespace {

    /// Convenience overload: fills in the EditorState-derived parameters that are
    /// always the same (canvas at k_menu_h origin, CANVAS_W/H, camera/scale from state).
    [[nodiscard]] std::optional<TileCoord> editor_screen_to_tile(int px, int py, const EditorState &state) noexcept {
      return screen_to_tile(px, py, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x, state.canvas.offset_y,
                            state.canvas.scale, state.elev_step_px, state.map.width, state.map.height,
                            effective_diamond_w(state.map), effective_diamond_h(state.map), state.map);
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

    void commit_collision_rect(EditorState &state) noexcept {
      if (!state.collision_dragging || state.map.tilesets.empty())
        return;
      const int tw = state.map.diamond_w();
      const int th = state.map.diamond_h();
      corundum::world::tilemap::CollisionRect candidate;
      if (state.col_drag_sub_tile) {
        candidate = pixel_to_tiled_rect(state.col_drag_anchor_win_x, state.col_drag_anchor_win_y,
                                        state.col_drag_cur_win_x, state.col_drag_cur_win_y, 0, k_menu_h, CANVAS_W,
                                        CANVAS_H, state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale,
                                        state.elev_step_px, state.map.height, tw, th, state.map);
      } else {
        candidate = snap_to_tile_rect(state.col_drag_anchor_col, state.col_drag_anchor_row, state.col_drag_cur_col,
                                      state.col_drag_cur_row);
      }
      const auto &cols = state.map.collisions;
      bool overlaps = false;
      for (std::size_t ci = 0; ci < cols.size() && !overlaps; ++ci) {
        overlaps =
            candidate.col < cols.cols[ci] + cols.col_spans[ci] && candidate.col + candidate.col_span > cols.cols[ci] &&
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

    void place_triangle_at(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = editor_screen_to_tile(win_x, win_y, state);
      if (!tc)
        return;
      const float col = static_cast<float>(tc->col);
      const float row = static_cast<float>(tc->row);
      constexpr float col_span = 1.f;
      constexpr float row_span = 1.f;
      auto &tris = state.map.collision_triangles;
      for (std::size_t i = 0; i < tris.size(); ++i) {
        if (tris.cols[i] == col && tris.rows[i] == row && tris.col_spans[i] == col_span &&
            tris.row_spans[i] == row_span) {
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
      tris.push_back(col, row, col_span, row_span, state.collision_tri_cut, elev);
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

    void commit_portal_rect(EditorState &state) noexcept {
      if (!state.portal_dragging || state.map.tilesets.empty())
        return;
      state.portal_dragging = false;
      const int col = std::min(state.portal_drag_anchor_col, state.portal_drag_cur_col);
      const int row = std::min(state.portal_drag_anchor_row, state.portal_drag_cur_row);
      const int w = std::abs(state.portal_drag_cur_col - state.portal_drag_anchor_col) + 1;
      const int h = std::abs(state.portal_drag_cur_row - state.portal_drag_anchor_row) + 1;
      state.portals.push_back({col, row, w, h, "", 0, 0});
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
        if (tc->col >= p.col && tc->col < p.col + p.w && tc->row >= p.row && tc->row < p.row + p.h) {
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
      const auto frac =
          pixel_to_fractional_tile(win_x, win_y, 0, k_menu_h, CANVAS_W, CANVAS_H, state.canvas.offset_x,
                                   state.canvas.offset_y, state.canvas.scale, state.elev_step_px, state.map.height,
                                   effective_diamond_w(state.map), effective_diamond_h(state.map), state.map);
      auto &cols = state.map.collisions;
      for (int i = static_cast<int>(cols.size()) - 1; i >= 0; --i) {
        const auto idx = static_cast<std::size_t>(i);
        if (frac.x >= cols.cols[idx] && frac.x < cols.cols[idx] + cols.col_spans[idx] && frac.y >= cols.rows[idx] &&
            frac.y < cols.rows[idx] + cols.row_spans[idx]) {
          cols.erase(idx);
          state.dirty = true;
          push_undo_checkpoint(state);
          return;
        }
      }
    }

    // ── Layer properties popup state ─────────────────────────────────────────
    bool layer_rename_requested = false;    ///< True when user double-clicked a layer.
    int layer_rename_idx = -1;              ///< Index of the layer being edited.
    char layer_rename_buf[256] = {};        ///< Buffer for the layer name.
    int layer_props_z_index = 0;            ///< Buffer for the layer's z_index, edited in the popup.
    bool layer_props_depth_sorted = false;  ///< Buffer for the layer's depth_sorted flag.
    bool layer_add_popup_requested = false; ///< True when the "+" button was clicked.

    // ── Fill-blocked popup state ─────────────────────────────────────────────
    bool show_fill_blocked_popup = false; ///< True when F was pressed on a non-ground layer.

    void add_layer(EditorState &state, LayerPreset preset) noexcept {
      state.map.layers.push_back(make_layer_from_preset(preset, state.map.width, state.map.height, state.map.layers));
      state.dirty = true;
      push_undo_checkpoint(state);
    }

    void delete_layer(EditorState &state) noexcept {
      if (state.map.layers.size() <= 1)
        return;
      const int idx = state.active_layer;
      state.map.layers.erase(state.map.layers.begin() + idx);
      if (state.active_layer >= static_cast<int>(state.map.layers.size()))
        state.active_layer = static_cast<int>(state.map.layers.size()) - 1;
      state.dirty = true;
      push_undo_checkpoint(state);
    }

    void handle_palette_click(EditorState &state, int win_x, int win_y) noexcept {
      const int panel_x = win_x - CANVAS_W;
      const int panel_y = win_y - k_menu_h;

      const int n_layers = static_cast<int>(state.map.layers.size());
      const int layer_strip_h = LAYER_TITLE_H + n_layers * LAYER_ROW_H;
      if (panel_y < LAYER_TITLE_H) {
        const float btn_y = (static_cast<float>(LAYER_TITLE_H) - LAYER_BTN_H) * 0.5f;
        if (panel_x >= LAYER_BTN_ADD_X && panel_x < LAYER_BTN_ADD_X + LAYER_BTN_W && panel_y >= btn_y &&
            panel_y < btn_y + LAYER_BTN_H) {
          layer_add_popup_requested = true;
          return;
        }
        if (panel_x >= LAYER_BTN_DEL_X && panel_x < LAYER_BTN_DEL_X + LAYER_BTN_W && panel_y >= btn_y &&
            panel_y < btn_y + LAYER_BTN_H) {
          delete_layer(state);
          return;
        }
        return;
      }
      if (panel_y < layer_strip_h) {
        const int idx = (panel_y - LAYER_TITLE_H) / LAYER_ROW_H;
        if (idx >= 0 && idx < n_layers) {
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && panel_x < PALETTE_W - 24) {
            layer_rename_requested = true;
            layer_rename_idx = idx;
            std::strncpy(layer_rename_buf, state.map.layers[static_cast<std::size_t>(idx)].name.c_str(),
                         sizeof(layer_rename_buf) - 1);
            layer_rename_buf[sizeof(layer_rename_buf) - 1] = '\0';
            layer_props_z_index = state.map.layers[static_cast<std::size_t>(idx)].z_index;
            layer_props_depth_sorted = state.map.layers[static_cast<std::size_t>(idx)].depth_sorted;
            return;
          }
          if (panel_x >= PALETTE_W - 24) {
            state.map.layers[static_cast<std::size_t>(idx)].visible =
                !state.map.layers[static_cast<std::size_t>(idx)].visible;
            push_undo_checkpoint(state);
          } else
            state.active_layer = idx;
        }
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

  } // namespace

  void handle_input(EditorState &state, MouseState &mouse, bool &running) {
    const ImGuiIO &io = ImGui::GetIO();
    const int mx = static_cast<int>(io.MousePos.x);
    const int my = static_cast<int>(io.MousePos.y);
    const bool over_canvas = mx >= 0 && mx < CANVAS_W && my >= k_menu_h && my < k_menu_h + CANVAS_H;
    const bool over_panel = mx >= CANVAS_W && my >= k_menu_h && my < k_menu_h + CANVAS_H;

    // Whether the canvas's own scrollbars are present this frame (mirrors the content-size vs.
    // window-size check in main.cpp that decides whether ImGui draws them), and whether the
    // --- Canvas pan/zoom ---
    state.canvas.update({0.f, static_cast<float>(k_menu_h)},
                        {static_cast<float>(CANVAS_W), static_cast<float>(CANVAS_H)},
                        /*zoom_to_cursor=*/true);

    // --- Keyboard ---
    if (!io.WantCaptureKeyboard) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (ImGui::IsKeyPressed(ImGuiKey_Q) && !io.KeyCtrl)) {
        if (state.dirty)
          request_exit_confirm(state);
        else
          running = false;
        return;
      }

      if (!state.map.tilesets.empty()) {
        const int dw = effective_diamond_w(state.map);
        const float step_px = static_cast<float>(dw) * state.canvas.scale * 0.5f;
        const float step_py = static_cast<float>(dw) * state.canvas.scale * 0.25f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
          state.canvas.offset_x -= step_px;
          auto [cx, cy] =
              clamp_camera(state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width,
                           state.map.height, dw, effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.canvas.offset_x = cx;
          state.canvas.offset_y = cy;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
          state.canvas.offset_x += step_px;
          auto [cx, cy] =
              clamp_camera(state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width,
                           state.map.height, dw, effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.canvas.offset_x = cx;
          state.canvas.offset_y = cy;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          state.canvas.offset_y -= step_py;
          auto [cx, cy] =
              clamp_camera(state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width,
                           state.map.height, dw, effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.canvas.offset_x = cx;
          state.canvas.offset_y = cy;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
          state.canvas.offset_y += step_py;
          auto [cx, cy] =
              clamp_camera(state.canvas.offset_x, state.canvas.offset_y, state.canvas.scale, state.map.width,
                           state.map.height, dw, effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.canvas.offset_x = cx;
          state.canvas.offset_y = cy;
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_G))
        state.show_grid = !state.show_grid;

      if (ImGui::IsKeyPressed(ImGuiKey_C)) {
        state.show_collisions = !state.show_collisions;
        if (!state.show_collisions)
          state.triangle_collision_mode = false;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_T) && state.show_collisions)
        state.triangle_collision_mode = !state.triangle_collision_mode;

      if (state.show_collisions && state.triangle_collision_mode) {
        using Cut = corundum::world::tilemap::TriangleCut;
        constexpr std::array<Cut, 4> order{Cut::NorthWest, Cut::NorthEast, Cut::SouthEast, Cut::SouthWest};
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) {
          for (int k = 0; k < 4; ++k) {
            if (state.collision_tri_cut == order[k]) {
              state.collision_tri_cut = order[(k + 1) % 4];
              break;
            }
          }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
          for (int k = 0; k < 4; ++k) {
            if (state.collision_tri_cut == order[k]) {
              state.collision_tri_cut = order[(k + 3) % 4];
              break;
            }
          }
        }
      } else if (state.show_elevation) {
        const int step = io.KeyShift ? 10 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
          state.selected_elevation =
              static_cast<uint8_t>(std::min(100, static_cast<int>(state.selected_elevation) + step));
        if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
          state.selected_elevation =
              static_cast<uint8_t>(std::max(0, static_cast<int>(state.selected_elevation) - step));
      } else if (state.show_ramps) {
        if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) || ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) {
          using corundum::world::tilemap::RampAxis;
          state.selected_ramp_axis =
              state.selected_ramp_axis == RampAxis::NorthSouth ? RampAxis::EastWest : RampAxis::NorthSouth;
        }
      }

      if (ImGui::IsKeyPressed(ImGuiKey_P))
        state.show_portals = !state.show_portals;

      if (ImGui::IsKeyPressed(ImGuiKey_E))
        state.show_elevation = !state.show_elevation;

      if (ImGui::IsKeyPressed(ImGuiKey_R))
        state.show_ramps = !state.show_ramps;

      if (ImGui::IsKeyPressed(ImGuiKey_W))
        state.show_walkability = !state.show_walkability;

      if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl) {
        if (!fill_ground_layer(state, state.selected_gid, state.selected_flip))
          show_fill_blocked_popup = true;
      }

      if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
        apply_redo(state);
      else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        apply_undo(state);

      if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && state.show_portals &&
          state.selected_portal >= 0 && state.selected_portal < static_cast<int>(state.portals.size())) {
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

      constexpr ImGuiKey num_keys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,
                                       ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9};
      for (int i = 0; i < 9; ++i) {
        if (ImGui::IsKeyPressed(num_keys[i]) && i < static_cast<int>(state.map.layers.size())) {
          state.active_layer = i;
          break;
        }
      }

      if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        try_save(state);
      }
    }

    // --- Mouse button down ---
    const bool popup_or_modal_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      mouse.left_held = true;
      if (!popup_or_modal_open && !ImGui::IsAnyItemActive()) {
        if (state.show_portals) {
          if (over_canvas)
            select_portal_at(state, mx, my);
        } else if (state.show_collisions) {
          if (over_canvas) {
            if (state.triangle_collision_mode)
              place_triangle_at(state, mx, my);
            else
              begin_collision_drag(state, mx, my);
          }
        } else if (state.show_elevation) {
          if (over_canvas) {
            paint_or_erase_elevation(state, mx, my, false);
            state.painting_active = true;
          }
        } else if (state.show_ramps) {
          if (over_canvas) {
            paint_or_erase_ramp(state, mx, my, false);
            state.painting_active = true;
          }
        } else {
          if (over_canvas) {
            paint_or_erase(state, mx, my, false);
            state.painting_active = true;
          } else if (over_panel)
            handle_palette_click(state, mx, my);
        }
      }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !popup_or_modal_open) {
      mouse.right_held = true;
      if (over_canvas) {
        if (state.show_portals)
          begin_portal_drag(state, mx, my);
        else if (state.show_collisions) {
          if (!remove_triangle_at(state, mx, my))
            remove_collision_at(state, mx, my);
        } else if (state.show_elevation)
          paint_or_erase_elevation(state, mx, my, true);
        else if (state.show_ramps)
          paint_or_erase_ramp(state, mx, my, true);
        else
          begin_erase_drag(state, mx, my);
      }
    }

    // --- Mouse button released ---
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

    // --- Mouse moved (continuous) ---
    {
      const auto tc = editor_screen_to_tile(mx, my, state);
      state.hover_tile_col = tc ? tc->col : -1;
      state.hover_tile_row = tc ? tc->row : -1;
    }

    if (state.show_portals) {
      if (state.portal_dragging)
        update_portal_drag(state, mx, my);
    } else if (state.show_collisions) {
      if (state.collision_dragging)
        update_collision_drag(state, mx, my);
    } else if (state.show_elevation) {
      if (state.painting_active && over_canvas && !popup_or_modal_open)
        paint_or_erase_elevation(state, mx, my, false);
      if (mouse.right_held && over_canvas)
        paint_or_erase_elevation(state, mx, my, true);
    } else if (state.show_ramps) {
      if (state.painting_active && over_canvas && !popup_or_modal_open)
        paint_or_erase_ramp(state, mx, my, false);
      if (mouse.right_held && over_canvas)
        paint_or_erase_ramp(state, mx, my, true);
    } else {
      if (state.painting_active && over_canvas && !popup_or_modal_open)
        paint_or_erase(state, mx, my, false);
      if (mouse.right_held && over_canvas)
        update_erase_drag(state, mx, my);
    }

    // --- Mouse wheel over the palette panel: Ctrl+scroll zooms (palette_tile_scale is user-
    // controlled, not auto-fit — see render_tile_grid.cpp), plain scroll pans vertically (the flow
    // layout has no columns, so only vertical scroll applies). ---
    const bool wheel_over_palette = over_panel && io.MouseWheel != 0.f && !state.map.tilesets.empty();
    if (wheel_over_palette && io.KeyCtrl) {
      constexpr float k_zoom_step = 0.1f;
      state.palette_tile_scale =
          std::clamp(state.palette_tile_scale + io.MouseWheel * k_zoom_step, k_palette_min_scale, k_palette_max_scale);
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

    // ── Add layer popup ──────────────────────────────────────────────────────
    if (layer_add_popup_requested) {
      ImGui::OpenPopup("Add Layer");
      layer_add_popup_requested = false;
    }

    if (ImGui::BeginPopupModal("Add Layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      constexpr LayerPreset presets[] = {LayerPreset::Ground, LayerPreset::FloorDetail, LayerPreset::Water,
                                         LayerPreset::Walls,  LayerPreset::Roof,        LayerPreset::Decor,
                                         LayerPreset::Blank};
      for (const LayerPreset preset : presets) {
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

    // ── Layer rename popup ──────────────────────────────────────────────────
    if (layer_rename_requested) {
      ImGui::OpenPopup("Rename Layer");
      layer_rename_requested = false;
    }

    if (ImGui::BeginPopupModal("Rename Layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Layer name:");
      ImGui::InputText("##rename", layer_rename_buf, sizeof(layer_rename_buf));

      ImGui::TextUnformatted("Z-index:");
      ImGui::InputInt("##zindex", &layer_props_z_index);
      if (layer_props_z_index < 0)
        layer_props_z_index = 0;

      ImGui::BeginDisabled(layer_props_z_index == 0);
      ImGui::Checkbox("Depth-sort with entities", &layer_props_depth_sorted);
      ImGui::EndDisabled();
      if (layer_props_z_index == 0)
        layer_props_depth_sorted = false;

      bool committed = false;
      if (ImGui::Button("Ok", ImVec2{120.f, 0.f})) {
        if (layer_rename_buf[0] != '\0') {
          auto &layer = state.map.layers[static_cast<std::size_t>(layer_rename_idx)];
          layer.name = layer_rename_buf;
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

    // ── Validation errors popup ──────────────────────────────────────────────
    if (state.show_validation_popup) {
      ImGui::OpenPopup("Validation Errors");
      state.show_validation_popup = false;
    }

    if (ImGui::BeginPopupModal("Validation Errors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(ImVec4{1.f, .6f, 0.f, 1.f},
                         "%zu problem(s) found in this map:", state.validation_errors.size());
      ImGui::Spacing();
      for (const auto &msg : state.validation_errors)
        ImGui::BulletText("%s", msg.c_str());
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::Button("Save Anyway", ImVec2{150.f, 0.f})) {
        if (auto r = save_tilemap(state); r)
          std::println("[Tilesmith] Saved with {} validation warning(s): {}", state.validation_errors.size(),
                       state.map_path.string());
        else
          std::println(stderr, "[Tilesmith] Save failed: {}", r.error());
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    // ── Fill-blocked popup ───────────────────────────────────────────────────
    if (show_fill_blocked_popup) {
      ImGui::OpenPopup("Fill Not Available");
      show_fill_blocked_popup = false;
    }

    if (ImGui::BeginPopupModal("Fill Not Available", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Fill only works on the ground layer (z_index 0).");
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2{120.f, 0.f}))
        ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

} // namespace tools::tilesmith
