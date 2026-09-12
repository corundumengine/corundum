// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/audio/audio_sys.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/handle.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/transition.hpp>

#include <expected>
#include <functional>
#include <string>

namespace corundum {

  /** @brief Game engine instance owning all system-level resources and game state.
   *
   * Owns systems directly (no virtual dispatch), the Scene (merged entity world +
   * game state), and all game assets. Lifecycle is intrinsic methods:
   *   initialize → run_loop → cleanup
   *
   * Returned by value from make_engine() and must stay trivially movable;
   * members must never store pointers or references into sibling members.
   *
   * @see initialize  One-time setup before the main loop.
   * @see run_loop    The main loop: input, fixed-step simulation, rendering.
   * @see cleanup     Resource teardown after the main loop.
   */
  struct Engine {
    // Declared window → gpu → renderer so reverse-order destruction is
    // renderer → gpu → window: sokol resources are released while the device is
    // still alive, and the device/window outlive the renderer.
    platform::Handle<platform::Window> window;
    platform::Handle<platform::GpuContext> gpu;
    platform::Handle<platform::Renderer> renderer;

    audio::AudioSystem audio;
    input::InputState input_state;
    render::RenderState render;

    core::GameConfig cfg;
    sprites::CharacterRegistry characters;
    corundum::world::FlagStore flags;
    dialogue::Registry graphs;
    item::Registry items;
    quest::Registry quests;
    world::Scene scene;
    bool entered_from_world = false; ///< True while inside an interior reached from the overworld.

    core::math::Colour clear_colour{.r = 30, .g = 30, .b = 35, .a = 255};
    debug::HudOverlay hud;
    core::time::LoopTimer timer{60.f};

    /** @brief Hook for custom dialogue EventActions not handled by the built-in dispatch.
     *
     *  Called for every EventAction in the pending queue. Return @c true to
     *  mark the event as handled (suppresses the unknown-event WARN).
     *  Default-empty; existing games are unaffected.
     */
    std::function<bool(Engine &, const dialogue::EventAction &)> on_event;

    /** @brief Hook called once per fixed step after world / dialogue-event processing.
     *
     *  Invoked inside the fixed-timestep loop, after process_dialogue_events()
     *  and before entity deletions are flushed. @p dt is the fixed timestep
     *  (timer.target_dt). Entities marked for deletion here are drained the
     *  same frame.
     *
     *  @note Marking entities for deletion in this hook sets the deletion flag,
     *  which forces this frame's interpolation alpha to 0 (see
     *  compute_interpolation_alpha in engine.cpp) — by design, because slot
     *  reuse from swap-and-pop invalidates the prev-transform snapshot.
     */
    std::function<void(Engine &, float dt)> on_fixed_update;

    /** @brief Take ownership of a backend-created platform handle.
     *
     *  The platform::Handle already carries the backend's destruction function, so
     *  these are plain moves. make_engine() and the NullPlatform test bundle use them
     *  to satisfy initialize()'s non-null window/renderer precondition.
     */
    void adopt_window(platform::Handle<platform::Window> value) noexcept {
      window = std::move(value);
    }

    void adopt_gpu(platform::Handle<platform::GpuContext> value) noexcept {
      gpu = std::move(value);
    }

    void adopt_renderer(platform::Handle<platform::Renderer> value) noexcept {
      renderer = std::move(value);
    }

    /** @brief Initialise all systems and load game assets.
     *
     *  @param[in] cfg Fully-loaded game configuration (move-ownership).
     *  @return ok on success, or std::unexpected with an error message.
     *  @pre window and renderer are non-null (make_engine() satisfies this).
     *       gpu may be left null (the main loop never touches it) for backends
     *       that don't need a GPU context.
     *  @post On failure, cleanup() has been run on the partially-initialised
     *        engine; discard it.
     */
    [[nodiscard]] std::expected<void, std::string> initialize(core::GameConfig &&cfg);

    /** @brief Advance the engine by exactly one frame: poll input, run pending
     *  fixed steps, render once with interpolation.
     *
     *  @return true while the loop should continue; false once quit is requested
     *          or the window has closed. run_loop() is equivalent to
     *          while (run_frame()) {}.
     *
     *  @pre initialize() must have returned successfully.
     *  @performance No heap allocation during the frame.
     */
    [[nodiscard]] bool run_frame() noexcept;

    /** @brief Run the main loop until the window closes or quit is requested.
     *
     *  Equivalent to while (run_frame()) {}.
     *
     *  @pre initialize() must have returned successfully.
     *  @performance No heap allocation during the loop.
     */
    void run_loop() noexcept;

    /** @brief Run the main loop followed by orderly teardown.
     *
     *  Equivalent to run_loop() followed by cleanup().
     *
     *  @pre initialize() must have returned successfully.
     */
    void run() noexcept;

    /** @brief Process all pending dialogue EventActions (built-in dispatch + on_event hook).
     *
     *  Walks scene.pending_dialogue_events and dispatches built-in events
     *  (play_sound, quest_start, quest_advance). For events not matched by built-in
     *  dispatch, calls on_event if set. Unhandled events print a WARN. Clears the
     *  pending list after processing.
     *
     *  Exposed for testability — game code normally does not call this directly.
     */
    void process_dialogue_events() noexcept;

    /** @brief Request a graceful shutdown.
     *
     *  Sets the quit flag; the next iteration of run_loop() will exit the main
     *  loop. Window closing is handled exclusively by cleanup(). Safe to call from
     *  any system during update().
     */
    void request_quit() noexcept;

    /** @brief Tear down resources after the main loop exits.
     *
     *  Shuts down audio and closes the window. Safe to call multiple times; after
     *  the first call, the only valid operation on the Engine is destruction.
     *
     *  @pre run_loop() must have returned, or initialize() has failed.
     */
    void cleanup() noexcept;

    /** @brief The single loaded tilemap, if the engine is in single-map mode.
     *
     *  @return Pointer to the active tilemap, or nullptr in World mode or before load.
     *  @see render::active_tilemap() for the RenderState-level accessor.
     */
    [[nodiscard]] const world::tilemap::Tilemap *active_tilemap() const noexcept {
      return corundum::render::active_tilemap(render);
    }

    /** @brief True once request_quit() or cleanup() has been called. */
    [[nodiscard]] bool quit_requested() const noexcept {
      return quit_;
    }

    /** @brief Live window width in screen pixels (0 before the first frame). */
    [[nodiscard]] int window_width() const noexcept {
      return window_width_;
    }

    /** @brief Live window height in screen pixels (0 before the first frame). */
    [[nodiscard]] int window_height() const noexcept {
      return window_height_;
    }

  private:
    bool quit_ = false;     ///< Set by request_quit()/cleanup(); see quit_requested().
    int window_height_ = 0; ///< Cached each frame by run_frame(); see window_height().
    int window_width_ = 0;  ///< Cached each frame by run_frame(); see window_width().
  };

} // namespace corundum
