// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/engine.hpp>

#include <expected>
#include <memory>
#include <span>
#include <string>

namespace corundum {

/** Startup options for the game process.
 *
 *  Only `show_debug_hud` and `show_splash` are derived from `argv` by `parse_engine_args()`;
 *  `config_path` is caller-set, so a game can point `make_engine()` at a
 *  different project config without a command-line flag. Every field has a
 *  sensible default so `parse_engine_args()` never fails.
 */
struct EngineOptions {
  /** Path to the game project's `game.json`; set before `make_engine()`. */
  std::string config_path{"data/game.json"};

  /** Enable the debug HUD overlay (set by `--debug`). */
  bool show_debug_hud{false};

  /** Play the game.json splash images at boot; `--no-splash` clears them for quick test runs. */
  bool show_splash{true};
};

/** Parse command-line arguments into an EngineOptions.
 *
 *  Supported flags:
 *    --debug        Enable the debug HUD overlay.
 *    --no-splash    Skip the boot splash.
 *
 *  @param args Argument vector from `main`, passed as `{argv, argc}`; the
 *        program-name entry is unrecognised and ignored.
 *  @return A fully-populated EngineOptions with defaults applied.
 *  @note `config_path` is not settable from the command line; set it on the
 *        returned options. Unrecognised flags are silently ignored (null entries
 *        are skipped) and the function is not `noexcept` — constructing the
 *        option strings may allocate.
 */
[[nodiscard]] EngineOptions parse_engine_args(std::span<const char *const> args);

/** Single-call factory: create platform, initialise engine, return a running Engine.
 *
 *  Combines platform creation and engine initialisation into one operation. On
 *  failure the caller receives an error description and no resources leak.
 *
 *  @param options Resolved command-line options.
 *  @return A fully-initialised, heap-allocated Engine on success, or an error message.
 *          Heap-allocated because Engine embeds the ~0.5 MB entity World, which must
 *          never sit on a stack frame (Windows' default main-thread stack is 1 MB).
 *  @post On success the Engine is ready for Engine::run(). On failure the window
 *        is closed, audio is shut down, and the Engine (and the platform objects
 *        it owns) is destroyed.
 */
[[nodiscard]] std::expected<std::unique_ptr<Engine>, std::string> make_engine(const EngineOptions &options);

} // namespace corundum
