// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/engine.hpp>

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace corundum {

  /** @brief Startup options for the game process.
   *
   *  Only @c show_debug_hud is derived from @c argv by @c parse_engine_args();
   *  @c config_path is caller-set, so a game can point @c make_engine() at a
   *  different project config without a command-line flag. Every field has a
   *  sensible default so @c parse_engine_args() never fails.
   */
  struct EngineOptions {
    /** @brief Path to the game project's @c game.json; set before @c make_engine(). */
    std::string config_path{"data/game.json"};

    /** @brief Enable the debug HUD overlay (set by @c --debug). */
    bool show_debug_hud{false};
  };

  /** @brief Parse command-line arguments into an EngineOptions.
   *
   *  Supported flags:
   *    --debug        Enable the debug HUD overlay.
   *
   *  @param[in] args Argument vector from @c main (excluding the program name).
   *  @return A fully-populated EngineOptions with defaults applied.
   *  @note @c config_path is not settable from the command line; set it on the
   *        returned options. Unrecognised flags are silently ignored (null entries
   *        are skipped) and the function is not @c noexcept — constructing the
   *        option strings may allocate.
   */
  [[nodiscard]] EngineOptions parse_engine_args(std::span<const char *const> args);

  /** @brief Single-call factory: create platform, initialise engine, return a running Engine.
   *
   *  Combines platform creation and engine initialisation into one operation. On
   *  failure the caller receives an error description and no resources leak.
   *
   *  @param[in] options Resolved command-line options.
   *  @return A fully-initialised, heap-allocated Engine on success, or an error message.
   *          Heap-allocated because Engine embeds the ~0.5 MB entity World, which must
   *          never sit on a stack frame (Windows' default main-thread stack is 1 MB).
   *  @note load_game_config() rejects non-positive window dimensions, so the
   *        narrowing conversion to the platform's unsigned size arguments is safe.
   *  @post On success the Engine is ready for Engine::run(). On failure the window
   *        is closed, audio is shut down, and the Engine (and the platform objects
   *        it owns) is destroyed.
   */
  [[nodiscard]] std::expected<std::unique_ptr<Engine>, std::string> make_engine(const EngineOptions &options);

} // namespace corundum
