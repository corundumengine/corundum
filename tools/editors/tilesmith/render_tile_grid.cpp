#include "render_tile_grid.hpp"
#include "common/tool_texture.hpp"
#include "layout.hpp"
#include <algorithm>
#include <cmath>
#include <corundum/gameplay/world/tilemap/loader.hpp>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <print>
#include <string>

namespace tools::tilemap {

  namespace {

    void fix_gids_after_removal(EditorState &state, int removed_first_gid, int removed_count) {
      for (auto &layer : state.map.layers) {
        for (auto &gid : layer.tiles) {
          if (gid != corundum::gameplay::world::tilemap::k_empty_tile &&
              static_cast<int>(gid) >= removed_first_gid + removed_count) {
            gid = static_cast<corundum::gameplay::world::tilemap::TileId>(static_cast<int>(gid) - removed_count);
          }
        }
        for (const auto &[_, cell] : layer.animated_cells) {
          for (auto &frame_gid : cell.frame_gids) {
            if (static_cast<int>(frame_gid) >= removed_first_gid + removed_count) {
              frame_gid =
                  static_cast<corundum::gameplay::world::tilemap::TileId>(static_cast<int>(frame_gid) - removed_count);
            }
          }
        }
      }
    }

    bool tileset_has_any_tile(const EditorState &state, int tileset_idx) {
      using corundum::gameplay::world::tilemap::TileId;
      if (tileset_idx < 0 || tileset_idx >= static_cast<int>(state.map.tilesets.size()))
        return false;
      const auto &ts = state.map.tilesets[static_cast<std::size_t>(tileset_idx)];
      const TileId range_start = ts.first_gid;
      const TileId range_end = static_cast<TileId>(static_cast<int>(ts.first_gid) + ts.tile_count);
      for (const auto &layer : state.map.layers) {
        for (TileId gid : layer.tiles) {
          if (gid >= range_start && gid < range_end)
            return true;
        }
        for (const auto &[_, cell] : layer.animated_cells) {
          for (TileId gid : cell.frame_gids) {
            if (gid >= range_start && gid < range_end)
              return true;
          }
        }
      }
      return false;
    }

    void do_add_tileset(tools::ToolApp &app, EditorState &state, TilemapTextureStore &texture_store,
                        std::vector<TilesetView> &tileset_views, const std::string &source_path) {
      using corundum::gameplay::world::tilemap::TileId;
      using corundum::gameplay::world::tilemap::TilemapTileset;
      using corundum::gameplay::world::tilemap::TilesetInfo;

      const auto tileset_result = corundum::gameplay::world::tilemap::load_tileset(source_path);
      if (!tileset_result)
        throw std::runtime_error(tileset_result.error());
      TilesetInfo info = std::move(*tileset_result);

      // Compute first_gid for the new tileset (appended at the end).
      TileId first_gid = 0;
      if (!state.map.tilesets.empty()) {
        const auto &last = state.map.tilesets.back();
        first_gid = static_cast<TileId>(static_cast<int>(last.first_gid) + last.tile_count);
      }

      TilemapTileset ts;
      ts.info = std::move(info);
      ts.first_gid = first_gid;
      ts.tile_count = ts.info.columns * ts.info.rows;

      state.map.tilesets.push_back(std::move(ts));

      tools::common::ToolTexture tex;
      try {
        tex = tools::common::load_tool_texture(app, state.map.tilesets.back().info.path);
      } catch (...) {
        state.map.tilesets.pop_back();
        throw;
      }
      texture_store.textures.push_back(std::move(tex));

      tileset_views = rebuild_tileset_views(app, state.map, texture_store);
      state.palette_tileset_idx = static_cast<int>(tileset_views.size()) - 1;
      state.palette_scroll_row = 0;
      state.palette_scroll_col = 0;
      state.dirty = true;
    }

    void do_remove_tileset(tools::ToolApp &app, EditorState &state, TilemapTextureStore &texture_store,
                           std::vector<TilesetView> &tileset_views, int idx) {
      using corundum::gameplay::world::tilemap::TileId;

      if (idx < 0 || idx >= static_cast<int>(state.map.tilesets.size()))
        return;

      const auto &removed_ts = state.map.tilesets[static_cast<std::size_t>(idx)];
      const int removed_first_gid = removed_ts.first_gid;
      const int removed_count = removed_ts.tile_count;

      // Destroy the GPU texture for this tileset
      tools::common::destroy_tool_texture(app, texture_store.textures[static_cast<std::size_t>(idx)]);
      texture_store.textures.erase(texture_store.textures.begin() + idx);

      state.map.tilesets.erase(state.map.tilesets.begin() + idx);

      // Shift first_gid for tilesets that came after the removed one.
      for (std::size_t i = static_cast<std::size_t>(idx); i < state.map.tilesets.size(); ++i)
        state.map.tilesets[i].first_gid =
            static_cast<TileId>(static_cast<int>(state.map.tilesets[i].first_gid) - removed_count);

      // Adjust existing tile GIDs so they still point to the correct tiles.
      fix_gids_after_removal(state, removed_first_gid, removed_count);

      tileset_views = rebuild_tileset_views(app, state.map, texture_store);

      if (state.palette_tileset_idx >= static_cast<int>(tileset_views.size()))
        state.palette_tileset_idx = std::max(0, static_cast<int>(tileset_views.size()) - 1);
      state.palette_scroll_row = 0;
      state.palette_scroll_col = 0;
      state.dirty = true;
    }

  } // namespace

  void render_tile_grid(tools::ToolApp &app, EditorState &state, TilemapTextureStore &texture_store,
                        std::vector<TilesetView> &tileset_views) {
    // Measure and render the tileset tab bar
    const float frame_h = ImGui::GetFrameHeight();
    const auto &style = ImGui::GetStyle();
    const int tab_h = static_cast<int>(
        std::ceil(frame_h + frame_h + style.TabBarBorderSize + style.ItemSpacing.y + style.WindowPadding.y * 0.5f));
    state.palette_tabbar_h = tab_h;

    const int n_layers = static_cast<int>(state.map.layers.size());
    const int layer_strip_h = LAYER_TITLE_H + n_layers * LAYER_ROW_H;
    const int n_tilesets = static_cast<int>(tileset_views.size());

    // State for deferred add/remove
    static bool show_add_popup = false;
    static char add_path_buf[512] = {};
    static bool show_remove_warning = false;
    static bool show_remove_in_use = false;
    static int remove_idx = -1;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowPos(ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(layer_strip_h)});
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(tab_h)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30.f / 255.f, 30.f / 255.f, 40.f / 255.f, 1.f});
    ImGui::Begin("Sprite Sheets", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    std::vector<int> to_remove;

    if (ImGui::BeginTabBar("##tilesets")) {
      for (int i = 0; i < n_tilesets; ++i) {
        const auto &ts = *tileset_views[static_cast<std::size_t>(i)].tileset;
        const std::string label = std::filesystem::path(ts.info.source).stem().string();
        bool keep = true;
        if (ImGui::BeginTabItem(label.c_str(), &keep)) {
          if (state.palette_tileset_idx != i) {
            state.palette_scroll_row = 0;
            state.palette_scroll_col = 0;
          }
          state.palette_tileset_idx = i;
          ImGui::EndTabItem();
        }
        if (!keep)
          to_remove.push_back(i);
      }

      // "+" button to add a new sprite sheet
      if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder))
        show_add_popup = true;

      ImGui::EndTabBar();
    }
    ImGui::End();

    // Process deferred removals (in reverse order so indices remain valid)
    if (!to_remove.empty()) {
      if (to_remove.size() == 1) {
        remove_idx = to_remove[0];
        if (tileset_has_any_tile(state, remove_idx)) {
          show_remove_in_use = true;
        } else {
          // Only show warning if there are tilesets after this one (will cause GID shift).
          if (remove_idx < n_tilesets - 1)
            show_remove_warning = true;
          else
            do_remove_tileset(app, state, texture_store, tileset_views, remove_idx);
        }
      }
      // If somehow multiple close requests in one frame, just handle the first.
    }

    // ── In-use error popup ────────────────────────────────────────────────────
    if (show_remove_in_use) {
      ImGui::OpenPopup("Cannot Remove Sprite Sheet");
      show_remove_in_use = false;
    }

    if (ImGui::BeginPopupModal("Cannot Remove Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      std::string msg;
      if (remove_idx >= 0 && remove_idx < n_tilesets) {
        const auto &ts = *tileset_views[static_cast<std::size_t>(remove_idx)].tileset;
        msg = std::format("Cannot remove '{}': the sprite sheet is in use by tiles in the map.\n\n"
                          "Erase all tiles using this sheet before removing it.",
                          std::filesystem::path(ts.info.source).stem().string());
      }
      ImGui::TextUnformatted(msg.c_str());
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2{120.f, 0.f})) {
        remove_idx = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Remove warning popup ──────────────────────────────────────────────────
    if (show_remove_warning) {
      ImGui::OpenPopup("Remove Sprite Sheet");
      show_remove_warning = false;
    }
    if (ImGui::BeginPopupModal("Remove Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      std::string msg;
      if (remove_idx >= 0 && remove_idx < n_tilesets) {
        const auto &ts = *tileset_views[static_cast<std::size_t>(remove_idx)].tileset;
        msg = std::format("Remove '{}'?\n\nThis will shift GIDs for all subsequent tilesets.\n"
                          "Existing tiles referencing shifted GIDs will display incorrectly.\n\n"
                          "Remove anyway?",
                          std::filesystem::path(ts.info.source).stem().string());
      }
      ImGui::TextUnformatted(msg.c_str());
      ImGui::Spacing();

      if (ImGui::Button("Remove", ImVec2{120.f, 0.f})) {
        do_remove_tileset(app, state, texture_store, tileset_views, remove_idx);
        remove_idx = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{120.f, 0.f})) {
        remove_idx = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // ── Add sprite sheet popup ────────────────────────────────────────────────
    if (show_add_popup) {
      ImGui::OpenPopup("Add Sprite Sheet");
      add_path_buf[0] = '\0';
      show_add_popup = false;
    }

    if (ImGui::BeginPopupModal("Add Sprite Sheet", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Path to sprite sheet JSON (relative to project root):");
      ImGui::Spacing();
      ImGui::InputText("##source_path", add_path_buf, sizeof(add_path_buf));
      ImGui::Spacing();

      bool added = false;
      if (ImGui::Button("Add", ImVec2{120.f, 0.f})) {
        std::string path_str(add_path_buf);
        // Strip quotes if the user pasted a quoted path
        if (path_str.size() >= 2 && path_str.front() == '"' && path_str.back() == '"')
          path_str = path_str.substr(1, path_str.size() - 2);
        if (!path_str.empty()) {
          try {
            do_add_tileset(app, state, texture_store, tileset_views, path_str);
            added = true;
          } catch (const std::runtime_error &e) {
            std::println(stderr, "[Tilesmith] Failed to add sprite sheet: {}", e.what());
          }
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{120.f, 0.f}) || added)
        ImGui::CloseCurrentPopup();

      ImGui::EndPopup();
    }

    // ── Tile image grid ───────────────────────────────────────────────────────
    constexpr int k_pivot_panel_h = 80;
    const int available_h = CANVAS_H - layer_strip_h - tab_h - k_pivot_panel_h;
    if (available_h <= 0 || tileset_views.empty())
      return;

    if (state.palette_tileset_idx >= n_tilesets)
      state.palette_tileset_idx = 0;

    const auto &view = tileset_views[static_cast<std::size_t>(state.palette_tileset_idx)];
    const auto &ts = *view.tileset;
    const float tex_w = static_cast<float>(view.tex_w);
    const float tex_h = static_cast<float>(view.tex_h);
    const ImTextureID tex_id = view.tex_id;

    // Target ~48px display cells for large tiles; cap at 2x so small tiles
    // (e.g. 16px) stay at their current 32px display size and don't blow up.
    constexpr float k_palette_target_px = 48.f;
    constexpr float k_palette_max_scale = 2.f;
    state.palette_tile_scale =
        std::clamp(k_palette_target_px / static_cast<float>(ts.info.frame_width), 0.05f, k_palette_max_scale);

    const int cell_w = std::max(1, static_cast<int>(static_cast<float>(ts.info.frame_width) * state.palette_tile_scale));
    const int cell_h =
        std::max(1, static_cast<int>(static_cast<float>(ts.info.frame_height) * state.palette_tile_scale));
    const int pal_cols = ts.info.columns;
    const int vis_cols = std::max(1, PALETTE_W / cell_w);
    const int vis_rows = available_h / cell_h;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowPos(ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(layer_strip_h + tab_h)});
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(available_h)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{30.f / 255.f, 30.f / 255.f, 40.f / 255.f, 1.f});
    ImGui::Begin("##tilegrid", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoMouseInputs);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    bool done = false;
    for (int vrow = 0; vrow < vis_rows && !done; ++vrow) {
      const int tile_row = vrow + state.palette_scroll_row;
      for (int vcol = 0; vcol < vis_cols; ++vcol) {
        const int actual_col = vcol + state.palette_scroll_col;
        if (actual_col >= pal_cols)
          break; // end of columns for this row — continue to next row, don't exit outer loop
        const int local_id = tile_row * pal_cols + actual_col;
        if (local_id >= ts.tile_count) {
          done = true;
          break;
        }
        const ImVec2 uv0{static_cast<float>(actual_col * ts.info.frame_width) / tex_w,
                         static_cast<float>(tile_row * ts.info.frame_height) / tex_h};
        const ImVec2 uv1{uv0.x + static_cast<float>(ts.info.frame_width) / tex_w,
                         uv0.y + static_cast<float>(ts.info.frame_height) / tex_h};
        ImGui::SetCursorPos(ImVec2{static_cast<float>(vcol * cell_w), static_cast<float>(vrow * cell_h)});
        {
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float hw = static_cast<float>(cell_w) * 0.5f;
          const float hh = static_cast<float>(cell_h) * 0.5f;
          constexpr ImU32 CHECKER_LIGHT = IM_COL32(180, 180, 180, 255);
          constexpr ImU32 CHECKER_DARK = IM_COL32(155, 155, 155, 255);
          auto *dl = ImGui::GetWindowDrawList();
          for (int cy = 0; cy < 2; ++cy)
            for (int cx = 0; cx < 2; ++cx)
              dl->AddRectFilled({p.x + cx * hw, p.y + cy * hh}, {p.x + (cx + 1) * hw, p.y + (cy + 1) * hh},
                                ((cx + cy) % 2 == 0) ? CHECKER_LIGHT : CHECKER_DARK);
        }
        ImGui::Image(tex_id, ImVec2{static_cast<float>(cell_w), static_cast<float>(cell_h)}, uv0, uv1);
      }
    }
    ImGui::End();

    // Selected tile highlight (foreground draw list, on top of tile images)
    const int sel_local_id = static_cast<int>(state.selected_gid) - static_cast<int>(ts.first_gid);
    if (sel_local_id >= 0 && sel_local_id < ts.tile_count) {
      const int sel_col = sel_local_id % ts.info.columns;
      const int sel_row = sel_local_id / ts.info.columns;
      const int vis_col_idx = sel_col - state.palette_scroll_col;
      const int vis_row_idx = sel_row - state.palette_scroll_row;
      if (vis_col_idx >= 0 && vis_col_idx < vis_cols && vis_row_idx >= 0 && vis_row_idx < vis_rows) {
        const ImVec2 p_min{static_cast<float>(CANVAS_W + vis_col_idx * cell_w),
                           static_cast<float>(layer_strip_h + tab_h + vis_row_idx * cell_h)};
        const ImVec2 p_max{p_min.x + static_cast<float>(cell_w), p_min.y + static_cast<float>(cell_h)};
        ImGui::GetForegroundDrawList()->AddRect(p_min, p_max, IM_COL32(255, 200, 0, 255), 0.f, 0, 2.f);
      }
    }

    // ── Origin (pivot) editor ─────────────────────────────────────────────────
    auto &active_ts = state.map.tilesets[static_cast<std::size_t>(state.palette_tileset_idx)];
    float pivot_x = active_ts.info.pivot_x;
    float pivot_y = active_ts.info.pivot_y;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowPos(ImVec2{static_cast<float>(CANVAS_W), static_cast<float>(CANVAS_H - k_pivot_panel_h)});
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(PALETTE_W), static_cast<float>(k_pivot_panel_h)});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{22.f / 255.f, 22.f / 255.f, 32.f / 255.f, 1.f});
    ImGui::Begin("##pivotpanel", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoTitleBar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Separator line at top of the panel.
    {
      const ImVec2 p = ImGui::GetWindowPos();
      ImGui::GetWindowDrawList()->AddLine(p, {p.x + PALETTE_W, p.y}, IM_COL32(80, 80, 100, 255), 1.f);
    }
    ImGui::SetCursorPosY(8.f);
    ImGui::TextDisabled("Origin");
    ImGui::SameLine(60.f);
    ImGui::SetNextItemWidth(88.f);
    const bool x_changed = ImGui::DragFloat("X##pivot_x", &pivot_x, 0.005f, 0.f, 1.f, "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Horizontal pivot\n0 = left edge, 1 = right edge");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(88.f);
    const bool y_changed = ImGui::DragFloat("Y##pivot_y", &pivot_y, 0.005f, 0.f, 1.f, "%.3f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Vertical pivot\n0 = bottom edge, 1 = top edge");

    if (x_changed || y_changed) {
      active_ts.info.pivot_x = std::clamp(pivot_x, 0.f, 1.f);
      active_ts.info.pivot_y = std::clamp(pivot_y, 0.f, 1.f);
      state.dirty = true;
    }

    // ── Footprint editor (per selected tile) ─────────────────────────────────
    ImGui::Spacing();
    const int fp_local_id = static_cast<int>(state.selected_gid) - static_cast<int>(active_ts.first_gid);
    const bool fp_sel_valid = fp_local_id >= 0 && fp_local_id < active_ts.tile_count;
    auto fp = fp_sel_valid ? corundum::gameplay::world::tilemap::get_tile_footprint(active_ts.info, fp_local_id)
                           : corundum::gameplay::world::tilemap::TileFootprint{};
    int fp_w = fp.w;
    int fp_h = fp.h;

    ImGui::TextDisabled("Footprint");
    ImGui::SameLine(60.f);
    ImGui::BeginDisabled(!fp_sel_valid);
    ImGui::SetNextItemWidth(80.f);
    const bool fw_changed = ImGui::DragInt("W##fp_w", &fp_w, 0.2f, 1, 32, "%d");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Footprint width in tiles");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.f);
    const bool fh_changed = ImGui::DragInt("H##fp_h", &fp_h, 0.2f, 1, 32, "%d");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Footprint height in tiles");
    ImGui::EndDisabled();

    if (fp_sel_valid && (fw_changed || fh_changed)) {
      fp_w = std::max(1, fp_w);
      fp_h = std::max(1, fp_h);
      if (fp_w == 1 && fp_h == 1)
        active_ts.info.tile_footprints.erase(fp_local_id);
      else
        active_ts.info.tile_footprints[fp_local_id] = {fp_w, fp_h};
      state.dirty = true;
    }

    ImGui::End();
  }

} // namespace tools::tilemap
