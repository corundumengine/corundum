// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <cassert>
#include <imgui.h>

namespace corundum::toolkit::editor {

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
   * spritesmith and loom.
   */
  struct CanvasController {
    /// @param min_scale Lower zoom limit enforced by update() and set_scale().
    /// @param max_scale Upper zoom limit enforced by update() and set_scale().
    explicit CanvasController(float min_scale = 0.125f, float max_scale = 16.f) noexcept
        : min_scale_{min_scale}, max_scale_{max_scale} {}

    float offset_x{};

    float offset_y{};

    float scale{1.f};

    /** @brief Process input once per frame (mid-mouse pan + wheel zoom).
     *
     * @param io             ImGui IO for the frame; injected so the controller keeps no
     *                       reference to ImGui's ambient global.
     * @param canvas_origin  Top-left of canvas area in screen coordinates.
     * @param canvas_size    Width/height of canvas area in screen coordinates.
     * @param zoom_to_cursor If true, zoom centers on mouse cursor position.
     */
    void update(const ImGuiIO &io, ImVec2 canvas_origin, ImVec2 canvas_size, bool zoom_to_cursor = false) {
      apply_pan(io, canvas_origin, canvas_size);
      apply_wheel_zoom(io, canvas_origin, canvas_size, zoom_to_cursor);
    }

    /// Set zoom, clamped to the controller's [min_scale, max_scale] range.
    void set_scale(float value) {
      scale = std::clamp(value, min_scale_, max_scale_);
    }

    /// Multiply the current zoom by @p factor, clamped to the controller's range.
    void zoom_by(float factor) {
      set_scale(scale * factor);
    }

    /** @brief Convert screen space to canvas space.
     * @pre scale > 0.
     */
    [[nodiscard]] ImVec2 screen_to_canvas(ImVec2 screen_pos, ImVec2 canvas_origin) const {
      assert(scale > 0.f);
      return {(screen_pos.x - canvas_origin.x + offset_x) / scale, (screen_pos.y - canvas_origin.y + offset_y) / scale};
    }

    /** @brief Convert canvas space to screen space.
     * @pre scale > 0.
     */
    [[nodiscard]] ImVec2 canvas_to_screen(ImVec2 canvas_pos, ImVec2 canvas_origin) const {
      assert(scale > 0.f);
      return {(canvas_pos.x * scale) - offset_x + canvas_origin.x, (canvas_pos.y * scale) - offset_y + canvas_origin.y};
    }

  private:
    float min_scale_{};

    float max_scale_{};

    bool panning_{};

    float pan_anchor_x_{};

    float pan_anchor_y_{};

    float pan_start_offset_x_{};

    float pan_start_offset_y_{};

    void apply_pan(const ImGuiIO &io, ImVec2 canvas_origin, ImVec2 canvas_size) {
      if (canvas_size.x <= 0.f || canvas_size.y <= 0.f)
        return;
      if (io.MouseClicked[ImGuiMouseButton_Middle] && is_cursor_over_canvas(io, canvas_origin, canvas_size)) {
        panning_ = true;
        pan_anchor_x_ = io.MousePos.x;
        pan_anchor_y_ = io.MousePos.y;
        pan_start_offset_x_ = offset_x;
        pan_start_offset_y_ = offset_y;
      }
      if (!panning_)
        return;
      offset_x = pan_start_offset_x_ - (io.MousePos.x - pan_anchor_x_);
      offset_y = pan_start_offset_y_ - (io.MousePos.y - pan_anchor_y_);
      if (!io.MouseDown[ImGuiMouseButton_Middle])
        panning_ = false;
    }

    void apply_wheel_zoom(const ImGuiIO &io, ImVec2 canvas_origin, ImVec2 canvas_size, bool zoom_to_cursor) {
      if (io.MouseWheel == 0.f || canvas_size.x <= 0.f || canvas_size.y <= 0.f)
        return;
      if (!is_cursor_over_canvas(io, canvas_origin, canvas_size))
        return;

      const float mouse_local_x = io.MousePos.x - canvas_origin.x;
      const float mouse_local_y = io.MousePos.y - canvas_origin.y;
      const float old_scale = scale;
      set_scale(scale * (io.MouseWheel > 0.f ? 1.15f : 1.f / 1.15f));

      if (!zoom_to_cursor)
        return;
      // Keep the canvas point under the cursor pinned in screen space as the scale changes.
      offset_x = ((mouse_local_x + offset_x) * (scale / old_scale)) - mouse_local_x;
      offset_y = ((mouse_local_y + offset_y) * (scale / old_scale)) - mouse_local_y;
    }

    [[nodiscard]] static bool is_cursor_over_canvas(const ImGuiIO &io, ImVec2 canvas_origin, ImVec2 canvas_size) {
      const float local_x = io.MousePos.x - canvas_origin.x;
      const float local_y = io.MousePos.y - canvas_origin.y;
      return local_x >= 0.f && local_y >= 0.f && local_x < canvas_size.x && local_y < canvas_size.y;
    }
  };

} // namespace corundum::toolkit::editor
