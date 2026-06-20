#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>
#include <corundum/render/data/render_state.hpp>
#include <corundum/resources/character_registry.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace corundum {

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
    std::unique_ptr<platform::Window> window;
    std::unique_ptr<platform::Renderer> renderer;

    input::InputState input_state;
    render::data::RenderState render;

    resources::CharacterRegistry characters;
    gameplay::dialogue::Registry graphs;
    core::GameConfig cfg;
    gameplay::world::Scene scene;

    core::time::LoopTimer timer{60.f};
    core::math::Colour clear_colour{30, 30, 35, 255};
    bool quit = false;
    bool show_debug_hud = false;
    float smoothed_fps = 0.f;
  };

  /** @brief Initialise all systems and load game assets.
   *  @param[in,out] engine      Uninitialised Engine.
   *  @param[in]     config_path Filesystem path to game.json.
   *  @return ok on success, or std::unexpected with an error message.
   *  @post On failure the window is closed and engine is partially initialised.
   */
  [[nodiscard]] std::expected<void, std::string> initialize(Engine &engine, std::string_view config_path);

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

  /** @brief Request a graceful shutdown of the engine.
   *
   *  Sets the quit flag; the next iteration of update() will exit the main loop.
   *  Safe to call from any system during update().
   *
   *  @param[in,out] engine Initialised Engine.
   */
  void request_quit(Engine &engine) noexcept;

} // namespace corundum
