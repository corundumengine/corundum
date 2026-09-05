#pragma once
#include <corundum/engine.hpp>

#include <expected>
#include <span>
#include <string>

namespace corundum {

  /** @brief Parsed command-line configuration for the game process.
   *
   *  Holds the resolved configuration derived from @c argv. Every field has a
   *  sensible default so that @c parse_engine_args() never fails.
   */
  struct EngineConfig {
    std::string config_path = "data/game.json";
    bool show_debug_hud = false;
  };

  /** @brief Parse command-line arguments into an EngineConfig.
   *
   *  Supported flags:
   *    --debug        Enable the debug HUD overlay.
   *
   *  @param[in] args Argument vector from @c main (excluding the program name).
   *  @return A fully-populated EngineConfig with defaults applied.
   *  @note Never fails — unrecognised flags are silently ignored.
   */
  [[nodiscard]] EngineConfig parse_engine_args(std::span<const char *const> args) noexcept;

  /** @brief Single-call factory: create platform, initialise engine, return a running Engine.
   *
   *  Combines platform creation and engine initialisation into one operation. On
   *  failure the caller receives an error description and no resources leak.
   *
   *  @param[in] config Resolved command-line configuration.
   *  @return A fully-initialised Engine on success, or an error message.
   *  @post On success the Engine is ready for run(). On failure the window is
   *        closed and audio is shut down; platform object memory is deliberately
   *        not freed (see detail::PlatformDeleter) and is reclaimed by the OS
   *        at process exit.
   */
  [[nodiscard]] std::expected<Engine, std::string> make_engine(const EngineConfig &config);

  /** @brief Execute the main game loop followed by orderly teardown.
   *
   *  Equivalent to run_loop(engine) followed by cleanup(engine).
   *
   *  @param[in,out] engine A fully-initialised Engine (from make_engine).
   *  @pre make_engine() must have returned successfully.
   *  @post The window is closed and the Engine is ready for destruction.
   */
  void run(Engine &engine) noexcept;

} // namespace corundum
