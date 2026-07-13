#pragma once
#include <algorithm>
#include <imgui.h>

namespace corundum::tool_host {

  /// Drawing context for an editor canvas — draw list + screen-space origin.
  struct CanvasContext {
    ImDrawList *dl{nullptr};
    ImVec2 origin{};
  };

  /// Mouse button state sampled once per frame.
  struct MouseState {
    bool left_held{};
    bool right_held{};
  };

  /** @brief Shared canvas pan/zoom controller.
   *
   * Captures the common pattern: middle-mouse pan with anchor, mouse-wheel
   * zoom with clamps, optional zoom-to-cursor, and screen↔canvas coordinate
   * transforms. Replaces the divergent implementations in tilesmith,
   * spritesmith and talesmith.
   */
  struct CanvasController {
    float offset_x = 0.f;
    float offset_y = 0.f;
    float scale = 1.f;
    bool left_held = false;
    bool right_held = false;

    /** @brief Process input once per frame (mid-mouse pan + wheel zoom).
     *
     * @param canvas_origin  Top-left of canvas area in screen coordinates.
     * @param canvas_size    Width/height of canvas area in screen coordinates.
     * @param zoom_to_cursor If true, zoom centers on mouse cursor position.
     * @param min_scale_     Minimum zoom limit.
     * @param max_scale_     Maximum zoom limit.
     */
    void update(ImVec2 canvas_origin, ImVec2 canvas_size, bool zoom_to_cursor = false, float min_scale_ = 0.125f,
                float max_scale_ = 16.f) {
      const ImGuiIO &io = ImGui::GetIO();

      // Middle-mouse pan
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        panning_ = true;
        pan_anchor_x_ = io.MousePos.x;
        pan_anchor_y_ = io.MousePos.y;
        pan_start_offset_x_ = offset_x;
        pan_start_offset_y_ = offset_y;
      }
      if (panning_) {
        offset_x = pan_start_offset_x_ - (io.MousePos.x - pan_anchor_x_);
        offset_y = pan_start_offset_y_ - (io.MousePos.y - pan_anchor_y_);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
          panning_ = false;
      }

      // Wheel zoom
      if (io.MouseWheel != 0.f && canvas_size.x > 0.f && canvas_size.y > 0.f) {
        const float old_scale = scale;
        scale = std::clamp(scale * (io.MouseWheel > 0.f ? 1.15f : 1.f / 1.15f), min_scale_, max_scale_);

        if (zoom_to_cursor) {
          const float mx = io.MousePos.x - canvas_origin.x;
          const float my = io.MousePos.y - canvas_origin.y;
          offset_x = mx - (mx - offset_x) * (scale / old_scale);
          offset_y = my - (my - offset_y) * (scale / old_scale);
        }
      }

      // Button held state
      left_held = ImGui::IsMouseDown(ImGuiMouseButton_Left);
      right_held = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    }

    /// Convert screen space to canvas space.
    [[nodiscard]] ImVec2 screen_to_canvas(ImVec2 screen_pos, ImVec2 canvas_origin) const {
      return {(screen_pos.x - canvas_origin.x + offset_x) / scale, (screen_pos.y - canvas_origin.y + offset_y) / scale};
    }

    /// Convert canvas space to screen space.
    [[nodiscard]] ImVec2 canvas_to_screen(ImVec2 canvas_pos, ImVec2 canvas_origin) const {
      return {canvas_pos.x * scale - offset_x + canvas_origin.x, canvas_pos.y * scale - offset_y + canvas_origin.y};
    }

  private:
    bool panning_ = false;
    float pan_anchor_x_ = 0.f, pan_anchor_y_ = 0.f;
    float pan_start_offset_x_ = 0.f, pan_start_offset_y_ = 0.f;
  };

} // namespace corundum::tool_host
