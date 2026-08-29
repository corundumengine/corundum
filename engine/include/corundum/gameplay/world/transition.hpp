#pragma once
#include <corundum/render/sys/render_sys.hpp>

#include <expected>
#include <string>

namespace corundum {
  struct Engine;
}

namespace corundum::gameplay::world {

  /** @brief (Re)initialise the overworld scene, optionally at a specific spawn tile.
   *
   *  Shared boot entry point used both by the initial world startup and by the
   *  interior→overworld return. When @p params is empty it boots at the manifest
   *  default (geometric centre); otherwise it boots at @p params and re-centres the
   *  3×3 streaming window on that tile's chunk so the player never spawns into an
   *  unstreamed chunk.
   *
   *  @param[in,out] engine Fully-initialised application state.
   *  @param[in]     params Optional overworld spawn tile (defaults to manifest centre).
   *  @return ok on success, or std::unexpected with an error message.
   *  @pre cfg.paths.world_manifest_path identifies the overworld manifest.
   */
  [[nodiscard]] std::expected<void, std::string> enter_world(corundum::Engine &engine,
                                                             const corundum::render::sys::WorldLoadParams &params = {});

  /** @brief Handle a pending map transition triggered by portal traversal.
   *
   *  When @p engine.scene.pending_transition is set, derives an action from the
   *  transition and the current render mode:
   *   - World + cross-map  : mark that we are inside an interior reached from the world,
   *                          then load the interior map.
   *   - SingleMap + cross-map : load the interior map, leaving the marker untouched.
   *   - any + return_to_world : re-initialise the overworld at the transition's spawn tile
   *                          (the exit portal always carries one).
   *  On failure the game is terminated with an error message.
   *
   *  @param[in,out] engine Fully-initialised application state.
   *  @note Called once per frame by the main loop in both single-map and world render modes.
   */
  void handle_map_transition(corundum::Engine &engine) noexcept;

} // namespace corundum::gameplay::world
