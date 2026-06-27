#include <corundum/engine_factory.hpp>
#include <corundum/platform/platform_factory.hpp>

#include <format>
#include <string_view>

namespace corundum {

  EngineConfig parse_engine_args(int argc, char *argv[]) noexcept {
    EngineConfig config{};
    for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--debug")
        config.show_debug_hud = true;
    }
    return config;
  }

  std::expected<Engine, std::string> make_engine(const EngineConfig &config) {
    auto cfg_result = core::load_game_config(config.config_path);
    if (!cfg_result)
      return std::unexpected(std::format("[engine] FATAL: {}", cfg_result.error()));

    auto cfg = std::move(*cfg_result);

    auto platform = platform::create_platform(config.window_width, config.window_height, cfg.window_title);
    if (!platform)
      return std::unexpected(platform.error());

    Engine engine{};
    engine.window = std::move(platform->window);
    engine.renderer = std::move(platform->renderer);
    engine.audio.backend = std::move(platform->audio_backend);
    engine.show_debug_hud = config.show_debug_hud;

    if (auto result = initialize(engine, std::move(cfg)); !result)
      return std::unexpected(result.error());

    return engine;
  }

  void run(Engine &engine) noexcept {
    update(engine);
    cleanup(engine);
  }

} // namespace corundum
