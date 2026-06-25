#include "render_portal.hpp"
#include "coords.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <imgui.h>

namespace tools::tilemap {
  using namespace tools::common;

  namespace {

    // Convert tile-grid coords (col, row) to isometric screen position.
    inline ImVec2 tile_to_iso(const CanvasContext &ctx, float col_f, float row_f, float half_tw, float half_th,
                              float x_shift, float cam_x, float cam_y) noexcept {
      return {ctx.origin.x + (col_f - row_f) * half_tw + x_shift - cam_x,
              ctx.origin.y + (col_f + row_f) * half_th - cam_y};
    }

    void draw_iso_portal(const CanvasContext &ctx, float col, float row, float w_tiles, float h_tiles, float half_tw,
                         float half_th, float x_shift, float cam_x, float cam_y, ImU32 fill, ImU32 outline) noexcept {
      const ImVec2 top = tile_to_iso(ctx, col, row, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 right = tile_to_iso(ctx, col + w_tiles, row, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 bottom = tile_to_iso(ctx, col + w_tiles, row + h_tiles, half_tw, half_th, x_shift, cam_x, cam_y);
      const ImVec2 left = tile_to_iso(ctx, col, row + h_tiles, half_tw, half_th, x_shift, cam_x, cam_y);
      ctx.dl->AddQuadFilled(top, right, bottom, left, fill);
      ctx.dl->AddQuad(top, right, bottom, left, outline, 1.f);
    }

  } // namespace

  void render_portals(CanvasContext ctx, const EditorState &state) {
    if (state.map.tilesets.empty())
      return;

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const float half_tw = static_cast<float>(dw) * state.tile_scale * 0.5f;
    const float half_th = static_cast<float>(dh) * state.tile_scale * 0.5f;
    const float x_shift = static_cast<float>(state.map.height) * half_tw;

    for (int i = 0; i < static_cast<int>(state.portals.size()); ++i) {
      const auto &p = state.portals[i];
      const bool selected = (i == state.selected_portal);

      draw_iso_portal(ctx, static_cast<float>(p.col), static_cast<float>(p.row), static_cast<float>(p.w),
                      static_cast<float>(p.h), half_tw, half_th, x_shift, state.camera.x, state.camera.y,
                      selected ? IM_COL32(0, 200, 200, 80) : IM_COL32(0, 200, 200, 45),
                      selected ? IM_COL32(0, 220, 220, 255) : IM_COL32(0, 220, 220, 180));

      // Label at the top corner of the portal rhombus
      const ImVec2 text_pos = tile_to_iso(ctx, static_cast<float>(p.col), static_cast<float>(p.row), half_tw, half_th,
                                          x_shift, state.camera.x, state.camera.y);
      const std::string stem = std::filesystem::path(p.target_map).stem().string();
      const std::string label = std::format("-> {} @ ({},{})", stem, p.spawn_col, p.spawn_row);
      ctx.dl->AddText({text_pos.x + 3.f, text_pos.y + 2.f},
                      selected ? IM_COL32(0, 255, 255, 255) : IM_COL32(0, 220, 220, 200), label.c_str());
    }
  }

  void render_portal_preview(CanvasContext ctx, const EditorState &state) {
    if (!state.portal_dragging || state.map.tilesets.empty())
      return;

    const int dw = effective_diamond_w(state.map);
    const int dh = effective_diamond_h(state.map);
    const float half_tw = static_cast<float>(dw) * state.tile_scale * 0.5f;
    const float half_th = static_cast<float>(dh) * state.tile_scale * 0.5f;
    const float x_shift = static_cast<float>(state.map.height) * half_tw;

    const int col = std::min(state.portal_drag_anchor_col, state.portal_drag_cur_col);
    const int row = std::min(state.portal_drag_anchor_row, state.portal_drag_cur_row);
    const int w = std::abs(state.portal_drag_cur_col - state.portal_drag_anchor_col) + 1;
    const int h = std::abs(state.portal_drag_cur_row - state.portal_drag_anchor_row) + 1;
    draw_iso_portal(ctx, static_cast<float>(col), static_cast<float>(row), static_cast<float>(w), static_cast<float>(h),
                    half_tw, half_th, x_shift, state.camera.x, state.camera.y, IM_COL32(0, 220, 220, 40),
                    IM_COL32(0, 220, 220, 230));
  }

  void render_portal_panel(EditorState &state) {
    if (!state.show_portals || state.selected_portal < 0 ||
        state.selected_portal >= static_cast<int>(state.portals.size()))
      return;

    auto &p = state.portals[static_cast<std::size_t>(state.selected_portal)];

    constexpr float PANEL_W = 340.f;

    if (state.show_portals_popup) {
      ImGui::OpenPopup("Portal");
      state.show_portals_popup = false;
    }

    if (ImGui::BeginPopupModal("Portal")) {
      constexpr int BUF_SIZE = 256;
      std::array<char, BUF_SIZE> buf{};
      std::copy_n(p.target_map.c_str(), std::min(static_cast<int>(p.target_map.size()), BUF_SIZE - 1), buf.data());
      ImGui::SetNextItemWidth(PANEL_W - 16.f);

      if (ImGui::InputText("##target_map", buf.data(), BUF_SIZE)) {
        p.target_map = buf.data();
        state.dirty = true;
      }
      ImGui::SameLine();
      ImGui::TextUnformatted("target");

      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("spawn col", &p.spawn_col))
        state.dirty = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.f);
      if (ImGui::InputInt("spawn row", &p.spawn_row))
        state.dirty = true;

      ImGui::TextDisabled("rect: col %d  row %d  w %d  h %d  [portal %d/%d]", p.col, p.row, p.w, p.h,
                          state.selected_portal + 1, static_cast<int>(state.portals.size()));

      if (ImGui::Button("Remove portal")) {
        state.portals.erase(state.portals.begin() + state.selected_portal);
        state.selected_portal = -1;
        state.dirty = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SetItemDefaultFocus();
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2{120.f, 0.f})) {
        state.show_portals_popup = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

} // namespace tools::tilemap
