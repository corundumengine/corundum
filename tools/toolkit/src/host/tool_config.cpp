// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/toolkit/host/tool_config.hpp>
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/content_source.hpp>
#include <corundum/core/environment.hpp>
#include <corundum/core/json_io.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

using nlohmann::json;

namespace corundum::toolkit::host {

namespace {

std::filesystem::path find_config_path(int argc, char *argv[]) {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string_view(argv[i]) == "--config")
      return argv[i + 1];
  }
  if (const std::optional<std::string> env = corundum::core::read_env("CORUNDUM_TOOLS_CONFIG"))
    return *env;
  return "tools/tools.json";
}

std::filesystem::path resolve_with_fallback(const std::filesystem::path &candidate, const std::string &fallback_name) {
  if (!candidate.empty() && std::filesystem::exists(candidate))
    return candidate;
  // Fallback relative to tools/toolkit/assets/ (searched relative to CWD)
  auto fallback = std::filesystem::path("tools") / "toolkit" / "assets" / fallback_name;
  if (std::filesystem::exists(fallback))
    return fallback;
  return candidate;
}

} // namespace

std::expected<ToolConfig, std::string> load_tool_config(int argc, char *argv[]) {
  const auto config_path = find_config_path(argc, argv);

  auto json_result = corundum::core::read_json(corundum::core::DirectoryContentSource{}, config_path);
  if (!json_result)
    return std::unexpected(std::format("Cannot load tool config: {}", json_result.error()));
  const json &j = *json_result;

  if (!j.is_object())
    return std::unexpected("Tool config must be a JSON object");

  const auto config_dir = std::filesystem::weakly_canonical(config_path).parent_path();

  const auto resolve = [&](const std::string &key) -> std::filesystem::path {
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

  ToolConfig config;
  config.data_root = resolve("data_root");
  config.font_path = resolve_with_fallback(resolve("font_path"), "NotoSans.ttf");
  config.icons_font_path = resolve_with_fallback(resolve("icons_font_path"), "NotoSans.ttf");
  config.theme_path = resolve_with_fallback(resolve("theme_path"), "editor_dark.json");
  config.dialogue_dir = resolve("dialogue_dir");
  config.quests_dir = resolve("quests_dir");

  config.elevation_step_px = j.value("elevation_step_px", config.elevation_step_px);
  config.max_step_height = j.value("max_step_height", config.max_step_height);
  config.tile_diamond_w = j.value("tile_diamond_w", config.tile_diamond_w);
  config.tile_diamond_h = j.value("tile_diamond_h", config.tile_diamond_h);

  return config;
}

} // namespace corundum::toolkit::host
