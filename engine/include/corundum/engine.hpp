#pragma once
#include <corundum/audio/sys/audio_sys.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/gpu_context.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/data/render_state.hpp>
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
      template<typename T>
      void operator()(T *) const noexcept { /* no-op */ }
    };
  } // namespace detail

  /** @brief Game engine instance owning all system-level resources and game state.
   *
   * Owns systems directly (no virtual dispatch), the Scene (merged ECS world +
   * game state), and all game assets. Lifecycle driven by free functions:
   *   initialize → update → cleanup
   *
   * @see initialize  One-time setup before the main loop.
   * @see update      Per-frame update loop.
   * @see cleanup     Resource teardown after the main loop.
   */
  struct Engine {
    std::unique_ptr<platform::Window, detail::PlatformDeleter> window;
    std::unique_ptr<platform::GpuContext, detail::PlatformDeleter> gpu;
    std::unique_ptr<platform::Renderer, detail::PlatformDeleter> renderer;

    audio::sys::AudioState audio;
    input::InputState input_state;
    render::data::RenderState render;

    resources::CharacterRegistry characters;
    gameplay::dialogue::Registry graphs;
    gameplay::quest::Registry quests;
    core::GameConfig cfg;
    gameplay::world::Scene scene;
    gameplay::FlagStore flags;

    int win_w = 0;  ///< Live window width in screen pixels, updated each frame.
    int win_h = 0;  ///< Live window height in screen pixels, updated each frame.

    core::time::LoopTimer timer{60.f};
    core::math::Colour clear_colour{30, 30, 35, 255};
    bool quit = false;
    bool show_debug_hud = false;
    float smoothed_fps = 0.f;

    /** @brief Hook for custom dialogue EventActions not handled by the built-in dispatch.
     *
     *  Called for every EventAction in the pending queue. Return @c true to
     *  mark the event as handled (suppresses the unknown-event WARN).
     *  Default-empty; existing games are unaffected.
     */
    std::function<bool(Engine &, const gameplay::dialogue::EventAction &)> on_event;

    /** @brief Hook called once per fixed step after world / dialogue-event processing.
     *
     *  Invoked inside the fixed-timestep loop, after process_dialogue_events()
     *  and before entity deletions are flushed. @p dt is the fixed timestep
     *  (timer.target_dt). Entities marked for deletion here are drained the
     *  same frame.
     */
    std::function<void(Engine &, float dt)> on_fixed_update;
  };

  /** @brief Initialise all systems and load game assets.
   *  @param[in,out] engine Uninitialised Engine.
   *  @param[in]     cfg    Fully-loaded game configuration (move-ownership).
   *  @return ok on success, or std::unexpected with an error message.
   *  @post On failure the window is closed and engine is partially initialised.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(Engine &engine, core::GameConfig &&cfg);

  /** @brief Run the main loop: poll input, fixed-timestep updates, rendering.
   *  @param[in,out] engine  Initialised Engine.
   *  @pre initialize() must have returned successfully.
   *  @performance No heap allocation during the loop.
   */
  void update(Engine &engine) noexcept;

  /** @brief Tear down resources after the main loop exits.
   *  @param[in,out] engine  Initialised Engine.
   *  @pre update() must have returned.
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
   *  Sets the quit flag; the next iteration of update() will exit the main loop.
   *  Safe to call from any system during update().
   *
   *  @param[in,out] engine Initialised Engine.
   */
  void request_quit(Engine &engine) noexcept;

} // namespace corundum
