// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/engine_factory.hpp>

#include <expected>
#include <memory>
#include <string>

namespace corundum::gameplay {

  class Gameplay;

  /** @brief The game process's owned pair: the gameplay framework and the engine it drives.
   *
   *  A passive record — the game drives the loop itself (`runtime->engine->run_loop()` then
   *  `cleanup()`), reads `runtime->engine->cfg` for settings paths, and installs the game's
   *  single slots (`runtime->gameplay->on_event`, `runtime->engine->on_fixed_update`).
   *
   *  @note Members are in declaration order deliberately: `gameplay` is declared first, so it
   *  is destroyed last and outlives the engine. Gameplay's hooks capture the gameplay owner, so
   *  reordering these members alphabetically produces a use-after-free in a hook. Do not reorder.
   */
  struct Runtime {
    std::unique_ptr<Gameplay> gameplay;

    std::unique_ptr<Engine> engine;

    Runtime();
    ~Runtime();

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    Runtime(Runtime &&) = delete;
    Runtime &operator=(Runtime &&) = delete;
  };

  /** @brief Create the platform, initialise the engine, then construct gameplay over it.
   *
   *  @param[in] options Resolved command-line options.
   *  @return A fully-initialised Runtime, or an error message.
   *  @post On success the engine is initialised and gameplay has loaded its content and
   *        registered its screens, hooks and fixed-step system. On failure nothing leaks.
   */
  [[nodiscard]] std::expected<std::unique_ptr<Runtime>, std::string> make_runtime(const EngineOptions &options);

} // namespace corundum::gameplay
