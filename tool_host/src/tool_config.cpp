#include <corundum/tool_host/tool_config.hpp>

#include <corundum/core/json_io.hpp>

#include <cstdlib>
#include <format>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::tool_host {

  // ── CLI argument scanning ─────────────────────────────────────────────────────

  static std::filesystem::path find_config_path(int argc, char *argv[]) {
    for (int i = 1; i < argc - 1; ++i) {
      if (std::string_view(argv[i]) == "--config")
        return argv[i + 1];
    }
    if (const char *env = std::getenv("CORUNDUM_TOOLS_CONFIG"))
      return env;
    return "tools/tools.json";
  }

  // ── Fallback: check if a file exists; return the path or a default ────────────

  static std::filesystem::path resolve_with_fallback(const std::filesystem::path &candidate,
                                                     const std::string &fallback_name) {
    if (!candidate.empty() && std::filesystem::exists(candidate))
      return candidate;
    // Fallback relative to tool_host/assets/ (searched relative to CWD)
    const auto fallback = std::filesystem::path("tool_host") / "assets" / fallback_name;
    if (std::filesystem::exists(fallback))
      return fallback;
    return candidate;
  }

  // ── Public API ────────────────────────────────────────────────────────────────

  std::expected<ToolConfig, std::string> load_tool_config(int argc, char *argv[]) {
    auto config_path = find_config_path(argc, argv);

    auto json_result = corundum::core::read_json(config_path);
    if (!json_result)
      return std::unexpected(std::format("Cannot load tool config: {}", json_result.error()));
    const json &j = *json_result;

    if (!j.is_object())
      return std::unexpected("Tool config must be a JSON object");

    // Resolve symlinks so relative paths work correctly (e.g. keystone/tools → ../corundum/tools).
    const auto config_dir = std::filesystem::weakly_canonical(config_path).parent_path();

    auto resolve = [&](const std::string &key) -> std::filesystem::path {
      if (!j.contains(key))
        return {};
      const std::string raw = j[key].get<std::string>();
      if (raw.empty())
        return {};
      auto p = std::filesystem::path(raw);
      if (p.is_relative())
        p = config_dir / p;
      return std::filesystem::weakly_canonical(p);
    };

    ToolConfig cfg;
    cfg.data_root = resolve("data_root");
    cfg.font_path = resolve_with_fallback(resolve("font_path"), "NotoSans.ttf");
    cfg.icons_font_path = resolve_with_fallback(resolve("icons_font_path"), "NotoSans.ttf");
    cfg.theme_path = resolve_with_fallback(resolve("theme_path"), "editor_dark.json");
    cfg.dialogue_dir = resolve("dialogue_dir");
    cfg.quests_dir = resolve("quests_dir");

    if (j.contains("elevation_step_px"))
      cfg.elevation_step_px = j["elevation_step_px"].get<float>();
    if (j.contains("max_step_height"))
      cfg.max_step_height = j["max_step_height"].get<unsigned int>();

    return cfg;
  }

} // namespace corundum::tool_host
