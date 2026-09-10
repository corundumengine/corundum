#pragma once

namespace corundum::world {

  struct MapView; // defined in update.hpp — forward-declared to keep this header light.

  /**
   * @brief Top-left world-pixel coordinate of the visible viewport.
   *
   * A small value type: public position/zoom fields, no invariant, no lifecycle.
   * The operations below are intrinsic camera behaviour — they mutate this camera
   * given per-call context, and never store collaborators.
   */
  class Camera {
  public:
    /// Camera feel constants — tuned for feel, deliberately not GameConfig fields.
    static constexpr float k_horiz_margin = 200.0f; ///< On-screen follow dead-zone width in px.
    static constexpr float k_vert_margin = 150.0f;  ///< On-screen follow dead-zone height in px.
    static constexpr float k_zoom_base = 1.1f;      ///< Multiplicative step per zoom notch.

    float x = 0.f;
    float y = 0.f;
    /// Camera-level zoom factor; 1.0 = no zoom. Renderer-only projection scale —
    /// never baked into tile/collision/walkability projection math.
    float zoom = 1.f;

    /**
     * @brief Update the camera position to follow a target with a dead zone.
     *
     * The camera only moves when the target crosses a dead zone around the
     * viewport centre. This prevents micro-jitter from small movements
     * (idle animation, velocity rounding).
     *
     *  @param[in] player_x Target X world position in px.
     *  @param[in] player_y Target Y world position in px.
     *  @param[in] map      Map dimensions for viewport clamping (world_w_px, world_h_px).
     *  @param[in] win_w    Viewport width in px.
     *  @param[in] win_h    Viewport height in px.
     *  @pre zoom > 0.
     *  @post Camera is clamped so the viewport does not extend beyond the map.
     *  @performance O(1). No heap allocation.
     */
    void follow_player(float player_x, float player_y, const MapView &map, float win_w, float win_h) noexcept;

    /**
     * @brief Adjust zoom by @p zoom_delta, keeping the world point under
     *  (@p anchor_x, @p anchor_y) visually fixed.
     *
     * Zoom steps multiplicatively: each notch multiplies zoom by `k_zoom_base`,
     * clamped to `[min_zoom, max_zoom]`. The anchor is whatever screen point should
     * stay put: the mouse cursor for scroll-wheel zoom, or the screen center for
     * keyboard/gamepad zoom (which has no cursor to aim with).
     *
     *  @param[in] zoom_delta Signed zoom amount for this call; a no-op at 0.
     *  @param[in] anchor_x   Screen-space X (window pixels) to keep fixed.
     *  @param[in] anchor_y   Screen-space Y (window pixels) to keep fixed.
     *  @param[in] min_zoom   Lower clamp bound (GameConfig::min_zoom).
     *  @param[in] max_zoom   Upper clamp bound (GameConfig::max_zoom).
     *  @performance O(1). No heap allocation.
     */
    void apply_zoom(float zoom_delta, float anchor_x, float anchor_y, float min_zoom, float max_zoom) noexcept;

    /**
     * @brief Center the camera on a world-space point, clamped to the world bounds.
     *
     * Positions the viewport so (@p target_x, @p target_y) sits at its center,
     * then clamps so the viewport does not extend beyond [0, world_w] × [0, world_h].
     * If the world is smaller than the effective viewport on an axis, the camera
     * is centered on the world along that axis (matching follow_player's edge
     * behavior) instead of clamping.
     *
     *  @param[in] target_x World-space X to center on, in px.
     *  @param[in] target_y World-space Y to center on, in px.
     *  @param[in] world_w  World width in px.
     *  @param[in] world_h  World height in px.
     *  @param[in] win_w    Viewport width in window px.
     *  @param[in] win_h    Viewport height in window px.
     *  @pre zoom > 0.
     *  @performance O(1). No heap allocation.
     */
    void center_on(float target_x, float target_y, float world_w, float world_h, float win_w, float win_h) noexcept;
  };

} // namespace corundum::world