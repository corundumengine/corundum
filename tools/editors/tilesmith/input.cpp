#include "input.hpp"
#include "coords.hpp"
#include "elevation_paint.hpp"
#include "layout.hpp"
#include "paint.hpp"
#include "save.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <imgui.h>
#include <print>

namespace tools::tilemap {

  namespace {

    void begin_collision_drag(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
      const int tw = state.map.tilesets[0].info.frame_width;
      const int th = state.map.tilesets[0].info.frame_height;
      corundum::gameplay::world::tilemap::CollisionRect candidate;
      if (state.col_drag_sub_tile) {
        candidate =
            pixel_to_tiled_rect(state.col_drag_anchor_win_x, state.col_drag_anchor_win_y, state.col_drag_cur_win_x,
                                state.col_drag_cur_win_y, state.camera.x, state.camera.y, state.tile_scale,
                                static_cast<float>(tw), static_cast<float>(th));
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
      state.map.collisions.push_back(candidate.col, candidate.row, candidate.col_span, candidate.row_span);
      state.dirty = true;
    }

    void place_triangle_at(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
      if (!tc)
        return;
      const float col = static_cast<float>(tc->col);
      const float row = static_cast<float>(tc->row);
      constexpr float col_span = 1.f;
      constexpr float row_span = 1.f;
      const auto &tris = state.map.collision_triangles;
      for (std::size_t i = 0; i < tris.size(); ++i) {
        if (tris.cols[i] == col && tris.rows[i] == row && tris.col_spans[i] == col_span &&
            tris.row_spans[i] == row_span && tris.cuts[i] == state.collision_tri_cut)
          return;
      }
      state.map.collision_triangles.push_back(col, row, col_span, row_span, state.collision_tri_cut);
      state.dirty = true;
    }

    void remove_triangle_at(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
      if (!tc)
        return;
      const float world_x = static_cast<float>(tc->col);
      const float world_y = static_cast<float>(tc->row);
      auto &tris = state.map.collision_triangles;
      for (int i = static_cast<int>(tris.size()) - 1; i >= 0; --i) {
        const auto idx = static_cast<std::size_t>(i);
        if (world_x >= tris.cols[idx] && world_x < tris.cols[idx] + tris.col_spans[idx] && world_y >= tris.rows[idx] &&
            world_y < tris.rows[idx] + tris.row_spans[idx]) {
          tris.erase(idx);
          state.dirty = true;
          return;
        }
      }
    }

    void begin_erase_drag(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
    }

    void begin_portal_drag(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
    }

    void select_portal_at(EditorState &state, int win_x, int win_y) noexcept {
      if (state.map.tilesets.empty())
        return;
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
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
      const auto tc = screen_to_tile(win_x, win_y, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                     state.tile_scale, state.map.width, state.map.height,
                                     effective_diamond_w(state.map), effective_diamond_h(state.map));
      if (!tc)
        return;
      const float world_x = static_cast<float>(tc->col);
      const float world_y = static_cast<float>(tc->row);
      auto &cols = state.map.collisions;
      for (int i = static_cast<int>(cols.size()) - 1; i >= 0; --i) {
        const auto idx = static_cast<std::size_t>(i);
        if (world_x >= cols.cols[idx] && world_x < cols.cols[idx] + cols.col_spans[idx] && world_y >= cols.rows[idx] &&
            world_y < cols.rows[idx] + cols.row_spans[idx]) {
          cols.erase(idx);
          state.dirty = true;
          return;
        }
      }
    }

    // ── Layer rename popup state ─────────────────────────────────────────────
    bool layer_rename_requested = false; ///< True when user double-clicked a layer.
    int layer_rename_idx = -1;           ///< Index of the layer being renamed.
    char layer_rename_buf[256] = {};     ///< Buffer for the new name.

    // ── Validation popup state ───────────────────────────────────────────────
    bool show_validation_popup = false;         ///< True when Ctrl+S found problems to confirm.
    std::vector<std::string> validation_errors; ///< Problems found by the last validate() run.

    void add_layer(EditorState &state) noexcept {
      const std::string name = std::format("Layer {}", state.map.layers.size() + 1);
      corundum::gameplay::world::tilemap::TilemapLayer layer;
      layer.name = name;
      layer.z_index = 0;
      layer.visible = true;
      layer.tiles.assign(static_cast<std::size_t>(state.map.width * state.map.height),
                         corundum::gameplay::world::tilemap::k_empty_tile);
      state.map.layers.push_back(std::move(layer));
      state.dirty = true;
    }

    void delete_layer(EditorState &state) noexcept {
      if (state.map.layers.size() <= 1)
        return;
      const int idx = state.active_layer;
      state.map.layers.erase(state.map.layers.begin() + idx);
      if (state.active_layer >= static_cast<int>(state.map.layers.size()))
        state.active_layer = static_cast<int>(state.map.layers.size()) - 1;
      state.dirty = true;
    }

    void handle_palette_click(EditorState &state, int win_x, int win_y) noexcept {
      const int panel_x = win_x - CANVAS_W;
      const int panel_y = win_y;

      const int n_layers = static_cast<int>(state.map.layers.size());
      const int layer_strip_h = LAYER_TITLE_H + n_layers * LAYER_ROW_H;
      if (panel_y < LAYER_TITLE_H) {
        const float btn_y = (static_cast<float>(LAYER_TITLE_H) - LAYER_BTN_H) * 0.5f;
        if (panel_x >= LAYER_BTN_ADD_X && panel_x < LAYER_BTN_ADD_X + LAYER_BTN_W && panel_y >= btn_y &&
            panel_y < btn_y + LAYER_BTN_H) {
          add_layer(state);
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
            return;
          }
          if (panel_x >= PALETTE_W - 24)
            state.map.layers[static_cast<std::size_t>(idx)].visible =
                !state.map.layers[static_cast<std::size_t>(idx)].visible;
          else
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
      const auto gid = palette_click_to_gid(grid_x, grid_y, ts, state.palette_scroll_row, state.palette_scroll_col,
                                            state.palette_tile_scale);
      if (gid)
        state.selected_gid = *gid;
    }

  } // namespace

  void handle_input(EditorState &state, MouseState &mouse, bool &running) {
    const ImGuiIO &io = ImGui::GetIO();
    const int mx = static_cast<int>(io.MousePos.x);
    const int my = static_cast<int>(io.MousePos.y);
    const bool over_canvas = mx >= 0 && mx < CANVAS_W && my >= 0 && my < CANVAS_H;
    const bool over_panel = mx >= CANVAS_W && my >= 0 && my < CANVAS_H;

    // --- Keyboard ---
    if (!io.WantCaptureKeyboard) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (ImGui::IsKeyPressed(ImGuiKey_Q) && !io.KeyCtrl)) {
        running = false;
        return;
      }

      state.camera_moved = false;
      if (!state.map.tilesets.empty()) {
        const int dw = effective_diamond_w(state.map);
        const float step_px = static_cast<float>(dw) * state.tile_scale * 0.5f; // half diamond width per arrow key
        const float step_py = static_cast<float>(dw) * state.tile_scale * 0.25f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
          state.camera.x -= step_px;
          state.camera = clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height, dw,
                                      effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.camera_moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
          state.camera.x += step_px;
          state.camera = clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height, dw,
                                      effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.camera_moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
          state.camera.y -= step_py;
          state.camera = clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height, dw,
                                      effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.camera_moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
          state.camera.y += step_py;
          state.camera = clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height, dw,
                                      effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
          state.camera_moved = true;
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
        using Cut = corundum::gameplay::world::tilemap::TriangleCut;
        constexpr std::array<Cut, 4> order{Cut::NW, Cut::NE, Cut::SE, Cut::SW};
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
      }

      if (ImGui::IsKeyPressed(ImGuiKey_P))
        state.show_portals = !state.show_portals;

      if (ImGui::IsKeyPressed(ImGuiKey_E))
        state.show_elevation = !state.show_elevation;

      if ((ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) && state.show_portals &&
          state.selected_portal >= 0 && state.selected_portal < static_cast<int>(state.portals.size())) {
        state.portals.erase(state.portals.begin() + state.selected_portal);
        state.selected_portal = -1;
        state.dirty = true;
      }

      if (ImGui::IsKeyPressed(ImGuiKey_X))
        state.selected_flip ^= corundum::gameplay::world::tilemap::k_flip_h;

      if (ImGui::IsKeyPressed(ImGuiKey_Y))
        state.selected_flip ^= corundum::gameplay::world::tilemap::k_flip_v;

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
        validation_errors = corundum::gameplay::world::tilemap::validate(state.map);
        if (validation_errors.empty()) {
          if (auto r = save_tilemap(state); r)
            std::println("[Tilesmith] Saved: {}", state.map_path.string());
          else
            std::println(stderr, "[Tilesmith] Save failed: {}", r.error());
        } else {
          show_validation_popup = true;
        }
      }
    }

    // --- Mouse button down ---
    const bool popup_or_modal_open = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      mouse.left_held = true;
      if (!popup_or_modal_open) {
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
          if (over_canvas)
            paint_or_erase_elevation(state, mx, my, false);
        } else {
          if (over_canvas)
            paint_or_erase(state, mx, my, false);
          else if (over_panel)
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
          if (state.triangle_collision_mode)
            remove_triangle_at(state, mx, my);
          else
            remove_collision_at(state, mx, my);
        } else if (state.show_elevation)
          paint_or_erase_elevation(state, mx, my, true);
        else
          begin_erase_drag(state, mx, my);
      }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !popup_or_modal_open) {
      state.panning = true;
      state.pan_anchor_x = mx;
      state.pan_anchor_y = my;
      state.pan_start_camera = state.camera;
    }

    // --- Mouse button released ---
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      mouse.left_held = false;
      if (!state.show_portals && state.show_collisions && !state.triangle_collision_mode && state.collision_dragging)
        commit_collision_rect(state);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
      mouse.right_held = false;
      if (state.show_portals && state.portal_dragging)
        commit_portal_rect(state);
      else if (state.erase_dragging)
        commit_erase_rect(state);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle))
      state.panning = false;

    // --- Mouse moved (continuous) ---
    if (state.panning) {
      state.camera.x = state.pan_start_camera.x - static_cast<float>(mx - state.pan_anchor_x);
      state.camera.y = state.pan_start_camera.y - static_cast<float>(my - state.pan_anchor_y);
      if (!state.map.tilesets.empty()) {
        state.camera = clamp_camera(state.camera, state.tile_scale, state.map.width, state.map.height,
                                    effective_diamond_w(state.map), effective_diamond_h(state.map), CANVAS_W, CANVAS_H);
      }
    } else {
      if (!state.map.tilesets.empty()) {
        const auto tc = screen_to_tile(mx, my, 0, 0, CANVAS_W, CANVAS_H, state.camera.x, state.camera.y,
                                       state.tile_scale, state.map.width, state.map.height,
                                       effective_diamond_w(state.map), effective_diamond_h(state.map));
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
        if (mouse.left_held && over_canvas && !popup_or_modal_open)
          paint_or_erase_elevation(state, mx, my, false);
        if (mouse.right_held && over_canvas)
          paint_or_erase_elevation(state, mx, my, true);
      } else {
        if (mouse.left_held && over_canvas && !popup_or_modal_open)
          paint_or_erase(state, mx, my, false);
        if (mouse.right_held && over_canvas)
          update_erase_drag(state, mx, my);
      }
    }

    // --- Mouse wheel (only over palette panel) ---
    if (over_panel && io.MouseWheel != 0.f && !state.map.tilesets.empty()) {
      const auto &ts = state.map.tilesets[static_cast<std::size_t>(state.palette_tileset_idx)];
      const int total_rows = (ts.tile_count + ts.info.columns - 1) / ts.info.columns;
      state.palette_scroll_row -= static_cast<int>(io.MouseWheel);
      state.palette_scroll_row = std::clamp(state.palette_scroll_row, 0, std::max(0, total_rows - 1));
    }
    if (over_panel && io.MouseWheelH != 0.f && !state.map.tilesets.empty()) {
      const auto &ts = state.map.tilesets[static_cast<std::size_t>(state.palette_tileset_idx)];
      const int cell_w =
          std::max(1, static_cast<int>(static_cast<float>(ts.info.frame_width) * state.palette_tile_scale));
      const int vis_cols = std::max(1, PALETTE_W / cell_w);
      state.palette_scroll_col -= static_cast<int>(io.MouseWheelH);
      state.palette_scroll_col = std::clamp(state.palette_scroll_col, 0, std::max(0, ts.info.columns - vis_cols));
    }

    // ── Layer rename popup ──────────────────────────────────────────────────
    if (layer_rename_requested) {
      ImGui::OpenPopup("Rename Layer");
      layer_rename_requested = false;
    }

    if (ImGui::BeginPopupModal("Rename Layer", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Layer name:");
      ImGui::InputText("##rename", layer_rename_buf, sizeof(layer_rename_buf));
      bool committed = false;
      if (ImGui::Button("Rename", ImVec2{120.f, 0.f})) {
        if (layer_rename_buf[0] != '\0') {
          state.map.layers[static_cast<std::size_t>(layer_rename_idx)].name = layer_rename_buf;
          state.dirty = true;
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
    if (show_validation_popup) {
      ImGui::OpenPopup("Validation Errors");
      show_validation_popup = false;
    }

    if (ImGui::BeginPopupModal("Validation Errors", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored(ImVec4{1.f, .6f, 0.f, 1.f}, "%zu problem(s) found in this map:", validation_errors.size());
      ImGui::Spacing();
      for (const auto &msg : validation_errors)
        ImGui::BulletText("%s", msg.c_str());
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::Button("Save Anyway", ImVec2{150.f, 0.f})) {
        if (auto r = save_tilemap(state); r)
          std::println("[Tilesmith] Saved with {} validation warning(s): {}", validation_errors.size(),
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
  }

} // namespace tools::tilemap
