// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/window_mode.hpp>
#include <corundum/ui/font_family.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::core {

  /// Font sizes and layout parameters for the dialogue panel.
  struct DialogueRenderConfig {
    /** @brief Font size for body text. */
    unsigned font_size_body = 22;

    /** @brief Font size for prompt text. */
    unsigned font_size_prompt = 18;

    /** @brief Font size for speaker text. */
    unsigned font_size_speaker = 26;

    /** @brief Vertical spacing between lines of text. */
    float line_spacing = 32.f;

    /** @brief Margin around the dialogue panel. */
    float margin = 20.f;

    /** @brief Fraction of window height occupied by the panel; must be in (0, 1). */
    float panel_height_frac = 0.32f;
  };

  /// Font sizes for the optional display role. Unlike every other role, whose text shares the
  /// body sizes, display text is sized per kind: one size for static headings, a larger one for
  /// transient banners. A game with no display family ignores both.
  struct DisplayRenderConfig {
    /** @brief Size of static headings (screen titles, the title screen's game name). */
    unsigned heading_size{48};

    /** @brief Size of transient banners (location, quest and level-up notices). */
    unsigned banner_size{64};
  };

  /// Contains all file paths required by the game engine and logic. Directory fields default to
  /// the standard project layout so a project following convention needs a near-empty game.json;
  /// only the per-game file names (the "fonts" families and tilemap_path) require explicit values.
  struct ResourcePaths {
    /** @brief Directory containing codex lore batch files. Defaults to "data/codex". */
    std::string codex_dir{"data/codex"};

    /** @brief Directory containing dialogue data files. Defaults to "data/dialogue". */
    std::string dialogue_dir{"data/dialogue"};

    /** @brief Directory containing all fonts. Defaults to "assets/fonts". */
    std::string font_dir{"assets/fonts"};

    /** @brief Per-role font families, indexed by ui::FontRole. A family's bold/italic files are
     *  optional; its regular file is required. The display family is optional and, when absent,
     *  resolves to the ui family. */
    std::array<ui::FontFamilyPaths, ui::k_font_role_count> fonts{};

    /** @brief Directory containing item data files. Defaults to "data/items". */
    std::string items_dir{"data/items"};

    /** @brief Directory containing fast-travel location batch files. Defaults to "data/locations". */
    std::string locations_dir{"data/locations"};

    /** @brief Directory containing portal definitions. Defaults to "data/portals". */
    std::string portals_dir{"data/portals"};

    /** @brief Directory containing quest data files. Defaults to "data/quests". */
    std::string quests_dir{"data/quests"};

    /** @brief Directory containing shop batch files. Defaults to "data/shops". */
    std::string shops_dir{"data/shops"};

    /** @brief Optional JSON catalog mapping sound names to file paths (relative to sounds_dir).
     *  Example: {"coin": "sfx/jingle_coin_01.ogg"}. Empty → names resolve to "{name}.ogg". */
    std::string sounds_catalog{"data/sounds.json"};

    /** @brief Directory containing sound (OGG) assets. */
    std::string sounds_dir{"data/sounds"};

    /** @brief Directory containing defined spawn point locations. Defaults to "data/spawn_points". */
    std::string spawn_points_dir{"data/spawn_points"};

    /** @brief Directory containing sprite sheet assets. Defaults to "data/sprite_sheets". */
    std::string sprites_dir{"data/sprite_sheets"};

    /** @brief Path to the main tilemap asset. */
    std::string tilemap_path;

    /** @brief Path to the world manifest JSON. Empty → single-tilemap mode. */
    std::string world_manifest_path;
  };

  /// Player identity and default placement, configurable via game.json "player" block.
  struct PlayerConfig {
    /** @brief Default spawn tile column (fractional tile-grid units). */
    float col = 8.f;

    /** @brief Sprite name for the player's idle animation. */
    std::string idle_sprite{"player_idle"};

    /** @brief Sprite name for the player's walk animation. */
    std::string walk_sprite{"player_walk"};

    /** @brief Default spawn tile row (fractional tile-grid units). */
    float row = 8.f;
  };

  /** @brief Fixed-update rate applied when game.json omits "simulation_fps", and the rate the engine's
   *  timer starts at before initialize() applies the configured value. */
  constexpr unsigned k_default_simulation_fps = 60;

  /** @brief In-game name used when a project sets neither "title" nor "window_title". */
  constexpr std::string_view k_default_game_title = "Corundum Engine";

  /// Full runtime configuration loaded from game.json. This struct is designed for cache efficiency by grouping related
  /// data together.
  struct GameConfig {
    /** @brief Machine-readable id of the game this config belongs to.
     *
     *  Written into saves via `game_id` and compared on load to refuse loading
     *  a save across games. Absent from game.json → empty string (no guard). */
    std::string game_id;

    /** @brief Fixed seed for Engine::rng, for reproducible runs. Absent → one draw from
     *  std::random_device at startup (logged). */
    std::optional<std::uint64_t> rng_seed{};

    /** @brief Target fixed-update rate for the simulation, in Hz. Rendering is paced independently by
     *  vsync, so this neither caps nor follows the display rate. */
    unsigned simulation_fps = k_default_simulation_fps;

    /** @brief Enable vsync for the render loop. */
    bool vsync = true;

    /** @brief Initial height of the game window in pixels. */
    float win_h = 600.f;

    /** @brief Initial width of the game window in pixels. */
    float win_w = 800.f;

    /** @brief Window mode at startup, before any user setting applies ("window_mode" in game.json). */
    WindowMode window_mode{WindowMode::Windowed};

    /** @brief Game name drawn on the Title screen ("title" in game.json).
     *
     *  Distinct from window_title: the OS caption and the in-game name can differ. At parse time
     *  an unset title falls back to window_title, then to k_default_game_title, so a loaded
     *  GameConfig always carries a non-empty title. */
    std::string title;

    /** @brief Window title shown in the OS title bar ("window_title" in game.json).
     *
     *  At parse time an unset window_title falls back to `title`, so the two are equal unless a
     *  project names them differently. */
    std::string window_title;

    /** @brief Whether the game opens on the Title screen.
     *
     *  Setting it to false starts directly in the world, which dev workflows and tests
     *  use to skip the framing screen. Only the gameplay framework reads it. */
    bool show_title = true;

    /** @brief Radius in tile-grid units for player interaction detection. */
    float interact_radius = 2.f;

    /** @brief Base movement speed of the player character. */
    float player_speed = 200.f;

    /** @brief Scaling factor applied to all character/entity sprites. */
    float character_scale = 2.f;

    /** @brief Initial Camera::zoom applied at startup, clamped to [min_zoom, max_zoom]. */
    float default_zoom = 1.f;

    /** @brief Screen pixels a tile is lifted per unit of elevation. */
    float elevation_step_px = 4.f;

    /** @brief Max elevation delta (same units as TilemapLayer::elevation) an entity can
     *  step between adjacent tiles without a ramp/stair bridging them. */
    unsigned int max_step_height = 4;

    /** @brief Maximum allowed Camera::zoom (most zoomed in). */
    float max_zoom = 3.f;

    /** @brief Minimum allowed Camera::zoom (most zoomed out). */
    float min_zoom = 0.5f;

    /** @brief Scaling factor applied to all tilemap assets. */
    float tile_scale = 2.f;

    /** @brief Rendering configuration specific to the dialogue system. */
    DialogueRenderConfig dialogue_render;

    /** @brief Display-role font sizes, parsed from the optional top-level "display" block. */
    DisplayRenderConfig display;

    /** @brief Grouped resource file paths for memory locality. */
    ResourcePaths paths{};

    /** @brief Player identity and default placement. */
    PlayerConfig player{};

    /** @brief Flag values seeded into the FlagStore during initialize(), before the first frame.
     *
     *  Lets a project author its starting state (gold, reputation, "intro_seen") in data rather
     *  than code. Absent or empty means no flags are seeded. */
    std::flat_map<std::string, int> starting_flags{};
  };

  /// Parses game.json at @p path and returns a validated GameConfig.
  /// @brief Loads and validates all engine configuration settings from the specified JSON file.
  /// @param path The filesystem path to game.json.
  /// @return std::expected<GameConfig, std::string> containing the validated config on success, or an error message on
  /// failure.
  [[nodiscard]] std::expected<GameConfig, std::string> load_game_config(const std::filesystem::path &path);

  /** @brief The game's display name for framing screens.
   *
   *  A project that sets only one of "title"/"window_title" gets that value for both; a project
   *  that sets neither gets k_default_game_title. load_game_config() resolves both fields already,
   *  so this only matters for a GameConfig built by hand (tests, embeddings).
   */
  [[nodiscard]] std::string_view game_title(const GameConfig &cfg) noexcept;

} // namespace corundum::core
