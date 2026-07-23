#pragma once

namespace corundum {
  struct Engine;
}

namespace corundum::gameplay::world {

  /**
   * @brief Handle a pending map transition triggered by portal traversal.
   *
   * When @p engine.scene.pending_transition is set, loads the target map, spawns a
   * new world at the destination portal position, and replaces the active scene.
   * On failure the game is terminated with an error message.
   *
   * @param[in,out] engine Fully-initialised application state.
   * @note Called once per frame by the main loop when in single-map render mode.
   */
  void handle_map_transition(corundum::Engine &engine) noexcept;

} // namespace corundum::gameplay::world
