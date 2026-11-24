// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/world_bounds.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace corundum {
struct Engine;
}

namespace corundum::core::math {
struct IsometricParams;
}

namespace corundum::render {
struct WorldLoadParams;
}

namespace corundum::world {

/** Which scene layout apply_spawn() should rebuild. */
enum class SpawnMode : uint8_t {
  World,     ///< Re-enter the streamed overworld.
  SingleMap, ///< Load a single tilemap by path.
};

/** Which point of a tile the camera centres on while framing a scene. */
enum class CameraAnchor : uint8_t {
  CellCenter, ///< Cell-centre anchor (tile_to_world_center) — the world-load path.
  TopVertex,  ///< Top-vertex anchor (tile_to_world) — cross-map transitions.
};

/** (Re)initialise the overworld scene, optionally at a specific spawn tile.
 *
 *  Shared boot entry point used both by the initial world startup and by the
 *  interior→overworld return. When `params` is empty it boots at the manifest
 *  default (geometric centre); otherwise it boots at `params` and re-centres the
 *  3×3 streaming window on that tile's chunk so the player never spawns into an
 *  unstreamed chunk.
 *
 *  @param engine Fully-initialised application state.
 *  @param params Optional overworld spawn tile (defaults to manifest centre).
 *  @return ok on success, or std::unexpected with an error message.
 *  @pre cfg.paths.world_manifest_path identifies the overworld manifest.
 */
[[nodiscard]] std::expected<void, std::string> enter_world(corundum::Engine &engine,
                                                           const corundum::render::WorldLoadParams &params);

/** Build the game's configured initial scene.
 *
 *  World mode (paths.world_manifest_path non-empty) enters the overworld at its manifest
 *  default spawn; single-map mode loads paths.tilemap_path and spawns the player by the usual
 *  precedence (per-map spawn point > game.json player block > built-in default). Called by the
 *  gameplay framework on New Game and on a no-title boot (and by engine-only games), so a new
 *  game always starts at the configured initial scene.
 *
 *  @param engine Fully-initialised application state; its scene is replaced.
 *  @return ok on success, or std::unexpected with an error message.
 */
[[nodiscard]] std::expected<void, std::string> load_initial_scene(corundum::Engine &engine);

/** Unload the area: release its render world and textures and replace the scene with an
 *  empty one (no player, empty UI stack). Flags are untouched; they belong to the playthrough.
 *
 *  @param engine Initialised application state.
 *  @pre engine.renderer() is non-null. Call between frames.
 *  @post render::mode(engine.render) is RenderMode::None, world::player_present(engine.scene) is false,
 *        engine.entered_from_world is false.
 */
void unload_world(corundum::Engine &engine);

/** Spawn the scene for a saved or transitioned location.
 *
 *  Reusable core of `handle_map_transition` and `corundum::save::load_game`:
 *  rebuilds the scene at `col`, `row` in `mode` (World re-enters the overworld
 *  via `enter_world`; SingleMap loads `id` as a tilemap path and spawns into
 *  it). When `zone` is non-empty it overwrites `scene.zone_id` after the spawn
 *  (saves carry it explicitly); otherwise the spawn derives it.
 *
 *  @param engine Fully-initialised application state.
 *  @param mode   Which layout to rebuild.
 *  @param id     Tilemap path (SingleMap) or world manifest id (World; ignored).
 *  @param zone   Zone id to force onto the new scene; empty = derive.
 *  @param col    Player spawn tile column.
 *  @param row    Player spawn tile row.
 *  @return ok on success, or std::unexpected with an error message.
 */
[[nodiscard]] std::expected<void, std::string>
apply_spawn(corundum::Engine &engine, SpawnMode mode, std::string_view id, std::string_view zone, float col, float row);

/** Handle a pending map transition triggered by portal traversal.
 *
 *  When `engine.scene.pending_transition` is set, derives an action from the
 *  transition and the current render mode:
 *   - World + cross-map  : mark that we are inside an interior reached from the world,
 *                          then load the interior map.
 *   - SingleMap + cross-map : load the interior map, leaving the marker untouched.
 *   - any + return_to_world : re-initialise the overworld at the transition's spawn tile
 *                          (the exit portal always carries one).
 *  On failure the game is terminated with an error message.
 *
 *  @param engine Fully-initialised application state.
 *  @note Called once per frame by the main loop in both single-map and world render modes.
 */
void handle_map_transition(corundum::Engine &engine) noexcept;

/** Clamp the configured default zoom and centre the camera on (`col`, `row`).
 *
 *  Projects the tile through `iso` using `anchor` and clamps the viewport to
 *  `bounds`. Shared by the initial single-map boot, `enter_world` and
 *  `apply_spawn` so scene framing has one implementation.
 *
 *  @param engine Fully-initialised application state.
 *  @param iso    Isometric projection parameters for the active map/world.
 *  @param col    Target tile column.
 *  @param row    Target tile row.
 *  @param bounds World extent in display pixels.
 *  @param anchor Which tile point to centre on.
 *  @pre cfg.min_zoom > 0, so the clamped default zoom is positive.
 *  @note Also restarts render interpolation from the framed scene (snapshot_previous_step).
 */
void frame_camera_on(corundum::Engine &engine, const corundum::core::math::IsometricParams &iso, float col, float row,
                     WorldBounds bounds, CameraAnchor anchor) noexcept;

} // namespace corundum::world
