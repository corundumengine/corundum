// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/runtime.hpp>
#include <corundum/settings/user_settings.hpp>

#include "core/elapsed_ms.hpp"
#include "core/warn_log.hpp"

#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace corundum::gameplay {

  Runtime::Runtime() = default;

  Runtime::~Runtime() = default;

  std::expected<std::unique_ptr<Runtime>, std::string> make_runtime(const EngineOptions &options) {
    const auto boot_start = std::chrono::steady_clock::now();
    auto engine_result = make_engine(options);
    if (!engine_result)
      return std::unexpected(engine_result.error());
    corundum::detail::info_log("[boot] engine: {:.1f} ms", corundum::detail::elapsed_ms(boot_start));

    // Apply the player's persisted settings over the boot config before the game root builds its
    // content. No user storage (no game_id or user-data directory) or a missing file keeps
    // game.json's values; a malformed or newer-schema file is a logged warning and likewise keeps them.
    if (const auto &storage = (*engine_result)->user_storage) {
      if (std::expected<void, std::string> loaded = settings::load(**engine_result, **storage); !loaded)
        corundum::detail::warn_log("[gameplay] WARN: could not load settings: {}", loaded.error());
    }

    auto runtime = std::make_unique<Runtime>();
    const auto gameplay_start = std::chrono::steady_clock::now();
    // Gameplay is constructed with the engine it drives and registers hooks capturing itself.
    // The Runtime member order keeps it alive until after the engine is destroyed.
    runtime->gameplay = std::make_unique<Gameplay>(**engine_result);
    runtime->engine = std::move(*engine_result);
    corundum::detail::info_log("[boot] gameplay content: {:.1f} ms", corundum::detail::elapsed_ms(gameplay_start));
    corundum::detail::info_log("[boot] total: {:.1f} ms", corundum::detail::elapsed_ms(boot_start));
    return runtime;
  }

} // namespace corundum::gameplay
