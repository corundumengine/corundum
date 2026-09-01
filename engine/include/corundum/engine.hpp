#pragma once
#include <corundum/audio/audio_sys.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/quest/registry.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/resources/character_registry.hpp>

#include <expected>
#include <functional>
#include <memory>
#include <string>

namespace corundum {

  namespace detail {
    /** @brief No-op deleter for platform objects whose destructors live in the platform library.
     *
     *  Engine holds platform resources through unique_ptr with this deleter so the
     *  engine library itself never needs the platform destructors — tests can link
     *  engine without linking the GLFW/sokol backend. Platform teardown is handled
     *  by cleanup() (window close, audio shutdown); OS reclaims the memory at exit.
     */
    struct PlatformDeleter {
      template <typename T> void operator()(T *) const noexcept { /* no-op */ }
    };
  } // namespace detail

  /** @brief Game engine instance owning all system-level resources and game state.
   *
   * Owns systems directly (no virtual dispatch), the Scene (merged ECS world +
   * game state), and all game assets. Lifecycle driven by free functions:
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
    std::unique_ptr<platform::GpuContext, detail::PlatformDeleter> gpu;
    std::unique_ptr<platform::Renderer, detail::PlatformDeleter> renderer;
    std::unique_ptr<platform::Window, detail::PlatformDeleter> window;

    audio::AudioSystem audio;
    input::InputState input_state;
    render::RenderState render;

    core::GameConfig cfg;
    resources::CharacterRegistry characters;
    corundum::world::FlagStore flags;
    dialogue::Registry graphs;
    item::Registry items;
    quest::Registry quests;
    gameplay::world::Scene scene;
    bool entered_from_world = false; ///< True while inside an interior reached from the overworld.

    int win_h = 0; ///< Live window height in screen pixels, updated each frame.
    int win_w = 0; ///< Live window width in screen pixels, updated each frame.

    core::math::Colour clear_colour{30, 30, 35, 255};
    debug::HudOverlay hud;
    bool quit = false;
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
  };

  /** @brief Initialise all systems and load game assets.
   *  @param[in,out] engine Uninitialised Engine.
   *  @param[in]     cfg    Fully-loaded game configuration (move-ownership).
   *  @return ok on success, or std::unexpected with an error message.
   *  @pre engine.window and engine.renderer are non-null (make_engine()
   *       satisfies this). engine.gpu may be left null (the main loop never
   *       touches it) for backends that don't need a GPU context.
   *  @post On failure, cleanup() has been run on the partially-initialised
   *        engine; discard it.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(Engine &engine, core::GameConfig &&cfg);

  /** @brief Advance the engine by exactly one frame: poll input, run pending
   *  fixed steps, render once with interpolation.
   *
   *  @return true while the loop should continue; false once quit is requested
   *          or the window has closed. run_loop() is equivalent to
   *          while (run_frame(engine)) {}.
   *
   *  @param[in,out] engine  Initialised Engine.
   *  @pre initialize() must have returned successfully.
   *  @performance No heap allocation during the frame.
   */
  [[nodiscard]] bool run_frame(Engine &engine) noexcept;

  /** @brief Run the main loop until the window closes or quit is requested.
   *
   *  Equivalent to while (run_frame(engine)) {}.
   *
   *  @param[in,out] engine  Initialised Engine.
   *  @pre initialize() must have returned successfully.
   *  @performance No heap allocation during the loop.
   */
  void run_loop(Engine &engine) noexcept;

  /** @brief Tear down resources after the main loop exits.
   *
   *  Shuts down audio and closes the window. Safe to call multiple times; after
   *  the first call, the only valid operation on the Engine is destruction.
   *
   *  @param[in,out] engine  Initialised Engine.
   *  @pre run_loop() must have returned, or initialize() has failed.
   */
  void cleanup(Engine &engine) noexcept;

  /** @brief Process all pending dialogue EventActions (built-in dispatch + on_event hook).
   *
   *  Walks engine.scene.pending_dialogue_events and dispatches built-in events
   *  (play_sound, quest_start, quest_advance). For events not matched by built-in
   *  dispatch, calls engine.on_event if set. Unhandled events print a WARN.
   *  Clears the pending list after processing.
   *
   *  Exposed for testability — game code normally does not call this directly.
   *
   *  @param[in,out] engine  Initialised Engine whose pending events are processed.
   */
  void process_dialogue_events(Engine &engine) noexcept;

  /** @brief Request a graceful shutdown of the engine.
   *
   *  Sets the quit flag; the next iteration of run_loop() will exit the main loop.
   *  Window closing is handled exclusively by cleanup(). Safe to call from any
   *  system during update().
   *
   *  @param[in,out] engine Initialised Engine.
   */
  void request_quit(Engine &engine) noexcept;

  /** @brief The single loaded tilemap, if the engine is in single-map mode.
   *  @param[in] engine Initialised Engine.
   *  @return Pointer to the active tilemap, or nullptr in World mode or before load.
   *  @see render::active_tilemap() for the RenderState-level accessor. */
  inline const gameplay::world::tilemap::Tilemap *active_tilemap(const Engine &engine) noexcept {
    return corundum::render::active_tilemap(engine.render);
  }

} // namespace corundum
