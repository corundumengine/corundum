// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/engine_factory.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/runtime.hpp>

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

    auto runtime = std::make_unique<Runtime>();
    // Gameplay is constructed with the engine it drives and registers hooks capturing itself.
    // The Runtime member order keeps it alive until after the engine is destroyed.
    runtime->gameplay = std::make_unique<Gameplay>(**engine_result);
    runtime->engine = std::move(*engine_result);
    return runtime;
  }

} // namespace corundum::gameplay
