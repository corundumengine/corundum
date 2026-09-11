#include <corundum/engine_factory.hpp>
#include <corundum/platform/platform_factory.hpp>

#include <format>
#include <string_view>

namespace corundum {

  EngineOptions parse_engine_args(std::span<const char *const> args) {
    EngineOptions config{};
    for (const char *const arg : args) {
      if (std::string_view(arg) == "--debug")
        config.show_debug_hud = true;
    }
    return config;
  }

  std::expected<Engine, std::string> make_engine(const EngineOptions &options) {
    auto cfg_result = core::load_game_config(options.config_path);
    if (!cfg_result)
      return std::unexpected(std::format("load config '{}': {}", options.config_path, cfg_result.error()));

    auto cfg = std::move(*cfg_result);

    auto platform =
        platform::create_platform(static_cast<unsigned>(cfg.win_w), static_cast<unsigned>(cfg.win_h), cfg.window_title);
    if (!platform)
      return std::unexpected(std::format("create platform: {}", platform.error()));

    Engine engine{};
    engine.adopt_window(std::move(platform->window));
    engine.adopt_gpu(std::move(platform->gpu));
    engine.adopt_renderer(std::move(platform->renderer));
    engine.audio.adopt_backend(std::move(platform->audio_backend));
    engine.hud.enabled = options.show_debug_hud;

    if (auto result = engine.initialize(std::move(cfg)); !result)
      return std::unexpected(result.error());

    return engine;
  }

} // namespace corundum
