#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::tool_host {

  /** @brief Configuration for editor tools, loaded via load_tool_config().
   *
   * Replaces direct use of GameConfig for tools. All relative paths are resolved
   * against the config file's parent directory, so tools can find data regardless
   * of CWD.
   */
  struct ToolConfig {
    std::filesystem::path data_root;
    std::filesystem::path font_path;
    std::filesystem::path icons_font_path;
    std::filesystem::path theme_path;
    std::filesystem::path dialogue_dir;
    std::filesystem::path quests_dir;
    float elevation_step_px = 4.f;
    unsigned int max_step_height = 4;
  };

  /** @brief Load ToolConfig from a JSON file.
   *
   * Resolution chain:
   *   1. --config <path> from command line
   *   2. CORUNDUM_TOOLS_CONFIG environment variable
   *   3. ./tools.json (CWD-relative)
   *
   * All relative paths in the JSON are resolved against the config file's parent
   * directory. If font_path or theme_path point to a non-existent file, the
   * helper falls back to tool_host/assets/{default} relative to the executable.
   *
   * @param argc  Argument count from main().
   * @param argv  Argument vector from main().
   * @return ToolConfig on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<ToolConfig, std::string> load_tool_config(int argc, char *argv[]);

} // namespace corundum::tool_host
