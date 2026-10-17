// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/game_config.hpp>
#include <corundum/core/user_storage.hpp>
#include <corundum/engine.hpp>
#include <corundum/engine_factory.hpp>
#include <corundum/platform/platform_factory.hpp>

#include <expected>
#include <format>
#include <memory>
#include <string>
#include <utility>

namespace corundum {

  void Engine::adopt_platform(platform::PlatformContext platform) {
    adopt_window(std::move(platform.window));
    adopt_gpu(std::move(platform.gpu));
    adopt_renderer(std::move(platform.renderer));
    audio.adopt_backend(std::move(platform.audio_backend));
  }

  std::expected<std::unique_ptr<Engine>, std::string> make_engine(const EngineOptions &options) {
    // Created first so the engine's content source outlives the platform adapters adopted below.
    auto engine = std::make_unique<Engine>();

    auto cfg_result = core::load_game_config(engine->content_source(), options.config_path);
    if (!cfg_result)
      return std::unexpected(std::format("load config '{}': {}", options.config_path, cfg_result.error()));

    auto cfg = std::move(*cfg_result);

    // --no-splash drops the images at the source, so the gameplay framework never sees a splash.
    if (!options.show_splash)
      cfg.paths.splash_images.clear();

    auto platform = platform::create_platform(engine->content_source(), cfg.win_w, cfg.win_h, cfg.window_title);
    if (!platform)
      return std::unexpected(std::format("create platform: {}", platform.error()));

    engine->adopt_platform(std::move(*platform));
    engine->hud.enabled = options.show_debug_hud;
    engine->user_storage = core::make_user_storage(cfg.game_id);

    if (auto result = engine->initialize(std::move(cfg)); !result)
      return std::unexpected(result.error());

    return engine;
  }

} // namespace corundum
