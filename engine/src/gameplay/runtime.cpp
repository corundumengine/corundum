// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/runtime.hpp>
#include <corundum/settings/user_settings.hpp>

#include "core/warn_log.hpp"

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace corundum::gameplay {

  Runtime::Runtime() = default;

  Runtime::~Runtime() = default;

  std::expected<std::unique_ptr<Runtime>, std::string> make_runtime(const EngineOptions &options) {
    auto engine_result = make_engine(options);
    if (!engine_result)
      return std::unexpected(engine_result.error());

    // Apply the player's persisted settings over the boot config before the game root builds its
    // content. A missing file keeps game.json's values; a malformed or newer-schema file is a
    // logged warning and likewise keeps them.
    if (std::expected<void, std::string> loaded = settings::load_default(**engine_result); !loaded)
      corundum::detail::warn_log("[gameplay] WARN: could not load settings: {}", loaded.error());

    auto runtime = std::make_unique<Runtime>();
    // Gameplay is constructed with the engine it drives and registers hooks capturing itself.
    // The Runtime member order keeps it alive until after the engine is destroyed.
    runtime->gameplay = std::make_unique<Gameplay>(**engine_result);
    runtime->engine = std::move(*engine_result);
    return runtime;
  }

} // namespace corundum::gameplay
