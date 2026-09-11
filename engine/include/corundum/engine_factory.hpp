#pragma once
#include <corundum/engine.hpp>

#include <expected>
#include <span>
#include <string>

namespace corundum {

  /** @brief Parsed command-line options for the game process.
   *
   *  Holds the resolved startup options derived from @c argv. Every field has a
   *  sensible default so that @c parse_engine_args() never fails.
   */
  struct EngineOptions {
    std::string config_path = "data/game.json";
    bool show_debug_hud = false;
  };

  /** @brief Parse command-line arguments into an EngineOptions.
   *
   *  Supported flags:
   *    --debug        Enable the debug HUD overlay.
   *
   *  @param[in] args Argument vector from @c main (excluding the program name).
   *  @return A fully-populated EngineOptions with defaults applied.
   *  @note Reports no failure (unrecognised flags are silently ignored) but is not
   *        @c noexcept — constructing the option strings may allocate.
   */
  [[nodiscard]] EngineOptions parse_engine_args(std::span<const char *const> args);

  /** @brief Single-call factory: create platform, initialise engine, return a running Engine.
   *
   *  Combines platform creation and engine initialisation into one operation. On
   *  failure the caller receives an error description and no resources leak.
   *
   *  @param[in] options Resolved command-line options.
   *  @return A fully-initialised Engine on success, or an error message.
   *  @post On success the Engine is ready for Engine::run(). On failure the window
   *        is closed, audio is shut down, and the Engine (and the platform objects
   *        it owns) is destroyed.
   */
  [[nodiscard]] std::expected<Engine, std::string> make_engine(const EngineOptions &options);

} // namespace corundum
