#pragma once

namespace corundum::gameplay::world {

  /// Top-left world-pixel coordinate of the visible viewport.
  struct Camera {
    float x = 0.f;
    float y = 0.f;
    /// Camera-level zoom factor; 1.0 = no zoom. Applied purely as a renderer
    /// projection scale (SokolRenderer::rebuild_proj()), never baked into
    /// tile/collision/walkability projection math.
    float zoom = 1.f;
  };

} // namespace corundum::gameplay::world
