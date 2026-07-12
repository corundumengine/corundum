#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/world/update.hpp>

namespace corundum {
  namespace render::data {
    struct RenderState;
  }
} // namespace corundum

namespace corundum::gameplay::world {

  /**
   * @brief Build a MapView from render state for the current frame's simulation step.
   *
   * Reads tile dimensions, isometric math, and collision views from the render
   * pipeline and packages them into the non-owning MapView consumed by update().
   * Handles both single-map and multi-chunk render modes.
   *
   * @param[in] render Current render state (collision data, tilemap info).
   * @param[in] cfg    Game configuration (tile scale).
   * @return A fully-populated MapView ready for the simulation step.
   */
  [[nodiscard]] MapView build_map_view(render::data::RenderState &render, const core::GameConfig &cfg) noexcept;

} // namespace corundum::gameplay::world
