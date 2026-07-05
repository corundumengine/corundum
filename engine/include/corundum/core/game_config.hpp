#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::core {

  /// Font sizes and layout parameters for the dialogue panel.
  struct DialogueRenderConfig {
    /** @brief Font size for speaker text. */
    unsigned font_size_speaker = 26;
    /** @brief Font size for body text. */
    unsigned font_size_body = 22;
    /** @brief Font size for prompt text. */
    unsigned font_size_prompt = 18;
    /** @brief Margin around the dialogue panel. */
    float margin = 20.f;
    /** @brief Vertical spacing between lines of text. */
    float line_spacing = 32.f;
    /** @brief Fraction of window height occupied by the panel; must be in (0, 1). */
    float panel_height_frac = 0.32f;
  };

  /// Contains all file paths required by the game engine and logic.
  struct ResourcePaths {
    /** @brief Directory containing all fonts. */
    std::string font_dir{};
    /** @brief Path to the primary game font file. */
    std::string game_font{};
    /** @brief Path to the UI-specific font file. */
    std::string ui_font{};
    /** @brief Path to the icons font file. */
    std::string icons_font{};
    /** @brief Path to the main tilemap asset. */
    std::string tilemap_path{};
    /** @brief Directory containing sprite sheet assets. */
    std::string sprites_dir{};
    /** @brief Directory containing defined spawn point locations. */
    std::string spawn_points_dir{};
    /** @brief Directory containing portal definitions. */
    std::string portals_dir{};
    /** @brief Directory containing dialogue data files. */
    std::string dialogue_dir{};
    /** @brief Directory containing quest data files. */
    std::string quests_dir{};
    /** @brief Directory containing sound (OGG) assets. */
    std::string sounds_dir{"data/sounds"};
    /** @brief Optional JSON catalog mapping sound names to file paths (relative to sounds_dir).
     *  Example: {"coin": "sfx/jingle_coin_01.ogg"}. Empty → names resolve to "{name}.ogg". */
    std::string sounds_catalog{"data/sounds.json"};
    /** @brief Path to the world manifest JSON. Empty → single-tilemap mode. */
    std::string world_manifest_path{};
  };

  /// Full runtime configuration loaded from game.json. This struct is designed for cache efficiency by grouping related
  /// data together.
  struct GameConfig {
    /** @brief Window title shown in the OS title bar. */
    std::string window_title = "Corundum Engine";
    /** @brief Initial width of the game window in pixels. */
    float win_w = 800.f;
    /** @brief Initial height of the game window in pixels. */
    float win_h = 600.f;
    /** @brief Target frame rate for the simulation (in FPS). */
    unsigned framerate = 60;
    /** @brief Enable vsync for the render loop. */
    bool vsync = true;

    /** @brief Radius in tile-grid units for player interaction detection. */
    float interact_radius = 2.f;
    /** @brief Base movement speed of the player character. */
    float player_speed = 200.f;

    /** @brief Scaling factor applied to all in-game sprites. */
    unsigned int sprite_scale = 2;
    /** @brief Scaling factor applied to all tilemap assets. */
    unsigned int tile_scale = 2;
    /** @brief Screen pixels a tile is lifted per unit of elevation. */
    float elevation_step_px = 4.f;
    /** @brief Max elevation delta (same units as TilemapLayer::elevation) an entity can
     *  step between adjacent tiles without a ramp/stair bridging them. */
    unsigned int max_step_height = 4;
    /** @brief Minimum allowed Camera::zoom (most zoomed out). */
    float min_zoom = 0.5f;
    /** @brief Maximum allowed Camera::zoom (most zoomed in). */
    float max_zoom = 3.f;

    /** @brief Grouped resource file paths for memory locality. */
    ResourcePaths paths{};
    /** @brief Rendering configuration specific to the dialogue system. */
    DialogueRenderConfig dialogue_render;
  };

  /// Parses game.json at @p path and returns a validated GameConfig.
  /// @brief Loads and validates all engine configuration settings from the specified JSON file.
  /// @param path The filesystem path to game.json.
  /// @return std::expected<GameConfig, std::string> containing the validated config on success, or an error message on
  /// failure.
  [[nodiscard]] std::expected<GameConfig, std::string> load_game_config(const std::filesystem::path &path);

} // namespace corundum::core
