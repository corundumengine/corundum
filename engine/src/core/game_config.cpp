// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cmath>
#include <corundum/core/game_config.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/ui/font_family.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
using nlohmann::json;

namespace corundum::core {

  namespace {

    /// Upper bound on the configured simulation rate. Real rates top out in the low hundreds, so
    /// anything past this is a corrupt or hand-edited config.
    constexpr unsigned int k_max_simulation_fps{1000};

    std::expected<unsigned int, std::string> get_positive_unsigned(const json &j, const std::string &key,
                                                                   unsigned int default_val, const fs::path &path) {
      if (!j.contains(key))
        return default_val;
      int v{0};
      try {
        v = j.at(key).get<int>();
      } catch (...) {
        return std::unexpected(std::format("game.json '{}' must be an integer: {}", key, path.string()));
      }
      if (v <= 0)
        return std::unexpected(std::format("game.json '{}' must be > 0: {}", key, path.string()));
      return static_cast<unsigned int>(v);
    }

    std::expected<float, std::string> get_positive_float(const json &j, const std::string &key, float default_val,
                                                         const fs::path &path) {
      if (!j.contains(key))
        return default_val;
      float v{NAN};
      try {
        v = j.at(key).get<float>();
      } catch (...) {
        return std::unexpected(std::format("game.json '{}' has wrong type: {}", key, path.string()));
      }
      if (v <= 0.f)
        return std::unexpected(std::format("game.json '{}' must be > 0: {}", key, path.string()));
      return v;
    }

    std::expected<std::string, std::string> get_nonempty_string(const json &j, const std::string &key,
                                                                const std::string &default_val, const fs::path &path) {
      if (!j.contains(key))
        return default_val;
      std::string v;
      try {
        v = j.at(key).get<std::string>();
      } catch (...) {
        return std::unexpected(std::format("game.json '{}' has wrong type: {}", key, path.string()));
      }
      if (v.empty())
        return std::unexpected(std::format("game.json '{}' must not be empty: {}", key, path.string()));
      return v;
    }

    std::expected<DialogueRenderConfig, std::string> parse_dialogue_render(const json &j, const fs::path &path) {
      DialogueRenderConfig dr;
      if (!j.contains("dialogue_render"))
        return dr;

      const auto &sub = j.at("dialogue_render");
      if (!sub.is_object())
        return std::unexpected(std::format("game.json 'dialogue_render' must be an object: {}", path.string()));

      const auto get_uint = [&](const std::string &key, unsigned default_val) -> std::expected<unsigned, std::string> {
        if (!sub.contains(key))
          return default_val;
        unsigned v{0};
        try {
          v = sub.at(key).get<unsigned>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'dialogue_render.{}' has wrong type: {}", key, path.string()));
        }
        return v;
      };

      const auto get_pos_float = [&](const std::string &key, float default_val) -> std::expected<float, std::string> {
        if (!sub.contains(key))
          return default_val;
        float v{NAN};
        try {
          v = sub.at(key).get<float>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'dialogue_render.{}' has wrong type: {}", key, path.string()));
        }
        return v;
      };

      {
        auto res = get_uint("font_size_speaker", dr.font_size_speaker);
        if (!res)
          return std::unexpected(res.error());
        dr.font_size_speaker = *res;
      }
      {
        auto res = get_uint("font_size_body", dr.font_size_body);
        if (!res)
          return std::unexpected(res.error());
        dr.font_size_body = *res;
      }
      {
        auto res = get_uint("font_size_prompt", dr.font_size_prompt);
        if (!res)
          return std::unexpected(res.error());
        dr.font_size_prompt = *res;
      }
      {
        auto res = get_pos_float("margin", dr.margin);
        if (!res)
          return std::unexpected(res.error());
        dr.margin = *res;
      }
      {
        auto res = get_pos_float("line_spacing", dr.line_spacing);
        if (!res)
          return std::unexpected(res.error());
        dr.line_spacing = *res;
      }

      if (sub.contains("panel_height_frac")) {
        float frac{NAN};
        try {
          frac = sub.at("panel_height_frac").get<float>();
        } catch (...) {
          return std::unexpected(
              std::format("game.json 'dialogue_render.panel_height_frac' has wrong type: {}", path.string()));
        }
        if (frac <= 0.f || frac >= 1.f)
          return std::unexpected(
              std::format("game.json 'dialogue_render.panel_height_frac' must be in (0, 1): {}", path.string()));
        dr.panel_height_frac = frac;
      }

      return dr;
    }

    std::expected<PlayerConfig, std::string> parse_player(const json &j, const fs::path &path) {
      PlayerConfig pc;
      if (!j.contains("player"))
        return pc;

      const auto &sub = j.at("player");
      if (!sub.is_object())
        return std::unexpected(std::format("game.json 'player' must be an object: {}", path.string()));

      const auto get_str = [&](const std::string &key,
                               const std::string &default_val) -> std::expected<std::string, std::string> {
        if (!sub.contains(key))
          return default_val;
        std::string v;
        try {
          v = sub.at(key).get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'player.{}' has wrong type: {}", key, path.string()));
        }
        if (v.empty())
          return std::unexpected(std::format("game.json 'player.{}' must not be empty: {}", key, path.string()));
        return v;
      };

      const auto get_non_neg_float = [&](const std::string &key,
                                         float default_val) -> std::expected<float, std::string> {
        if (!sub.contains(key))
          return default_val;
        float v{NAN};
        try {
          v = sub.at(key).get<float>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'player.{}' has wrong type: {}", key, path.string()));
        }
        if (v < 0.f)
          return std::unexpected(std::format("game.json 'player.{}' must be >= 0: {}", key, path.string()));
        return v;
      };

      {
        auto res = get_str("walk_sprite", pc.walk_sprite);
        if (!res)
          return std::unexpected(res.error());
        pc.walk_sprite = std::move(*res);
      }
      {
        auto res = get_str("idle_sprite", pc.idle_sprite);
        if (!res)
          return std::unexpected(res.error());
        pc.idle_sprite = std::move(*res);
      }
      {
        auto res = get_non_neg_float("col", pc.col);
        if (!res)
          return std::unexpected(res.error());
        pc.col = *res;
      }
      {
        auto res = get_non_neg_float("row", pc.row);
        if (!res)
          return std::unexpected(res.error());
        pc.row = *res;
      }

      return pc;
    }

    std::expected<void, std::string> parse_window_settings(const json &j, GameConfig &cfg, const fs::path &path) {
      {
        auto res = get_positive_float(j, "win_w", cfg.win_w, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.win_w = *res;
      }
      {
        auto res = get_positive_float(j, "win_h", cfg.win_h, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.win_h = *res;
      }
      if (j.contains("simulation_fps")) {
        unsigned fr{0};
        try {
          fr = j.at("simulation_fps").get<unsigned>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'simulation_fps' has wrong type: {}", path.string()));
        }
        if (fr == 0)
          return std::unexpected(std::format("game.json 'simulation_fps' must be > 0: {}", path.string()));
        if (fr > k_max_simulation_fps)
          return std::unexpected(
              std::format("game.json 'simulation_fps' must be <= {}: {}", k_max_simulation_fps, path.string()));
        cfg.simulation_fps = fr;
      }
      if (j.contains("vsync")) {
        try {
          cfg.vsync = j.at("vsync").get<bool>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'vsync' must be a boolean: {}", path.string()));
        }
      }
      if (j.contains("window_mode")) {
        const json &value = j.at("window_mode");
        const std::optional<core::WindowMode> mode =
            value.is_string() ? core::parse_window_mode(value.get<std::string>()) : std::nullopt;
        if (!mode)
          return std::unexpected(
              std::format(R"(game.json 'window_mode' must be "windowed" or "fullscreen": {})", path.string()));
        cfg.window_mode = *mode;
      }
      return {};
    }

    std::expected<void, std::string> parse_gameplay_settings(const json &j, GameConfig &cfg, const fs::path &path) {
      {
        auto res = get_positive_float(j, "interact_radius", cfg.interact_radius, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.interact_radius = *res;
      }
      {
        auto res = get_positive_float(j, "player_speed", cfg.player_speed, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.player_speed = *res;
      }
      {
        auto res = get_positive_float(j, "character_scale", cfg.character_scale, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.character_scale = *res;
      }
      {
        auto res = get_positive_float(j, "tile_scale", cfg.tile_scale, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.tile_scale = *res;
      }
      {
        auto res = get_positive_float(j, "elevation_step_px", cfg.elevation_step_px, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.elevation_step_px = *res;
      }
      {
        auto res = get_positive_unsigned(j, "max_step_height", cfg.max_step_height, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.max_step_height = *res;
      }
      return {};
    }

    std::expected<void, std::string> parse_zoom_settings(const json &j, GameConfig &cfg, const fs::path &path) {
      {
        auto res = get_positive_float(j, "min_zoom", cfg.min_zoom, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.min_zoom = *res;
      }
      {
        auto res = get_positive_float(j, "max_zoom", cfg.max_zoom, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.max_zoom = *res;
      }
      {
        auto res = get_positive_float(j, "default_zoom", cfg.default_zoom, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.default_zoom = *res;
      }
      if (cfg.min_zoom > cfg.max_zoom)
        return std::unexpected(std::format("game.json 'min_zoom' must be <= 'max_zoom': {}", path.string()));
      return {};
    }

    std::expected<ui::FontFamilyPaths, std::string> parse_font_family(const json &fonts, std::string_view role,
                                                                      const fs::path &path) {
      const std::string prefix = std::format("fonts.{}", role);
      if (!fonts.contains(role))
        return std::unexpected(std::format("game.json '{}' is required: {}", prefix, path.string()));
      const json &family = fonts.at(role);
      if (!family.is_object())
        return std::unexpected(std::format("game.json '{}' must be an object: {}", prefix, path.string()));

      const auto read_style = [&](std::string_view style, bool required) -> std::expected<std::string, std::string> {
        const std::string key = std::format("{}.{}", prefix, style);
        if (!family.contains(style)) {
          if (required)
            return std::unexpected(std::format("game.json '{}' is required: {}", key, path.string()));
          return std::string{};
        }
        std::string value;
        try {
          value = family.at(style).get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("game.json '{}' has wrong type: {}", key, path.string()));
        }
        if (value.empty())
          return std::unexpected(std::format("game.json '{}' must not be empty: {}", key, path.string()));
        return value;
      };

      ui::FontFamilyPaths out;
      auto regular = read_style("regular", true);
      if (!regular)
        return std::unexpected(regular.error());
      out.regular = std::move(*regular);

      auto bold = read_style("bold", false);
      if (!bold)
        return std::unexpected(bold.error());
      out.bold = std::move(*bold);

      auto bold_italic = read_style("bold_italic", false);
      if (!bold_italic)
        return std::unexpected(bold_italic.error());
      out.bold_italic = std::move(*bold_italic);

      auto italic = read_style("italic", false);
      if (!italic)
        return std::unexpected(italic.error());
      out.italic = std::move(*italic);
      return out;
    }

    std::expected<void, std::string> parse_fonts(const json &j, GameConfig &cfg, const fs::path &path) {
      if (!j.contains("fonts"))
        return {};
      const json &fonts = j.at("fonts");
      if (!fonts.is_object())
        return std::unexpected(std::format("game.json 'fonts' must be an object: {}", path.string()));

      constexpr std::array k_roles{
          std::pair{ui::FontRole::Dialogue, std::string_view{"dialogue"}},
          std::pair{ui::FontRole::Quest, std::string_view{"quest"}},
          std::pair{ui::FontRole::Ui, std::string_view{"ui"}},
      };
      for (const auto &[role, name] : k_roles) {
        auto family = parse_font_family(fonts, name, path);
        if (!family)
          return std::unexpected(family.error());
        cfg.paths.fonts[static_cast<std::size_t>(role)] = std::move(*family);
      }

      // The display family is optional; a game without one draws display text in the ui family.
      const auto display_slot = static_cast<std::size_t>(ui::FontRole::Display);
      if (!fonts.contains("display")) {
        cfg.paths.fonts[display_slot] = cfg.paths.fonts[static_cast<std::size_t>(ui::FontRole::Ui)];
        return {};
      }

      auto family = parse_font_family(fonts, "display", path);
      if (!family)
        return std::unexpected(family.error());
      cfg.paths.fonts[display_slot] = std::move(*family);
      return {};
    }

    std::expected<void, std::string> parse_resource_paths(const json &j, GameConfig &cfg, const fs::path &path) {
      {
        auto res = get_nonempty_string(j, "font_dir", cfg.paths.font_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.font_dir = std::move(*res);
      }
      {
        auto res = parse_fonts(j, cfg, path);
        if (!res)
          return std::unexpected(res.error());
      }
      {
        auto res = get_nonempty_string(j, "tilemap_path", cfg.paths.tilemap_path, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.tilemap_path = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "sprites_dir", cfg.paths.sprites_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.sprites_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "spawn_points_dir", cfg.paths.spawn_points_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.spawn_points_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "portals_dir", cfg.paths.portals_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.portals_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "dialogue_dir", cfg.paths.dialogue_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.dialogue_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "codex_dir", cfg.paths.codex_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.codex_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "locations_dir", cfg.paths.locations_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.locations_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "shops_dir", cfg.paths.shops_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.shops_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "quests_dir", cfg.paths.quests_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.quests_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "items_dir", cfg.paths.items_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.items_dir = std::move(*res);
      }
      {
        auto res = get_nonempty_string(j, "sounds_dir", cfg.paths.sounds_dir, path);
        if (!res)
          return std::unexpected(res.error());
        cfg.paths.sounds_dir = std::move(*res);
      }
      if (j.contains("sounds_catalog")) {
        try {
          cfg.paths.sounds_catalog = j.at("sounds_catalog").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'sounds_catalog' has wrong type: {}", path.string()));
        }
      }
      if (j.contains("world_manifest_path")) {
        try {
          cfg.paths.world_manifest_path = j.at("world_manifest_path").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'world_manifest_path' has wrong type: {}", path.string()));
        }
      }
      return {};
    }

    std::expected<std::flat_map<std::string, int>, std::string> parse_starting_flags(const json &j,
                                                                                     const fs::path &path) {
      std::flat_map<std::string, int> flags;
      if (!j.contains("starting_flags"))
        return flags;

      const json &sub = j.at("starting_flags");
      if (!sub.is_object())
        return std::unexpected(std::format("game.json 'starting_flags' must be an object: {}", path.string()));

      for (const auto &[key, value] : sub.items()) {
        if (key.empty())
          return std::unexpected(std::format("game.json 'starting_flags' keys must not be empty: {}", path.string()));
        if (!value.is_number_integer())
          return std::unexpected(
              std::format("game.json 'starting_flags.{}' must be an integer: {}", key, path.string()));
        try {
          flags.insert_or_assign(key, value.get<int>());
        } catch (...) {
          return std::unexpected(std::format("game.json 'starting_flags.{}' is out of range: {}", key, path.string()));
        }
      }
      return flags;
    }

    std::expected<DisplayRenderConfig, std::string> parse_display_render(const json &j, const fs::path &path) {
      DisplayRenderConfig display;
      if (!j.contains("display"))
        return display;

      const auto &sub = j.at("display");
      if (!sub.is_object())
        return std::unexpected(std::format("game.json 'display' must be an object: {}", path.string()));

      const auto get_uint = [&](const std::string &key, unsigned default_val) -> std::expected<unsigned, std::string> {
        if (!sub.contains(key))
          return default_val;
        unsigned v{0};
        try {
          v = sub.at(key).get<unsigned>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'display.{}' has wrong type: {}", key, path.string()));
        }
        return v;
      };

      auto heading = get_uint("heading_size", display.heading_size);
      if (!heading)
        return std::unexpected(heading.error());
      display.heading_size = *heading;

      auto banner = get_uint("banner_size", display.banner_size);
      if (!banner)
        return std::unexpected(banner.error());
      display.banner_size = *banner;
      return display;
    }

  } // namespace

  std::expected<GameConfig, std::string> load_game_config(const fs::path &path) {
    auto j_result = read_json(path, "game.json");
    if (!j_result)
      return std::unexpected(std::move(j_result).error());
    json j = std::move(*j_result);

    if (!j.is_object())
      return std::unexpected(std::format("game.json must be a JSON object: {}", path.string()));

    GameConfig cfg;

    {
      auto res = get_nonempty_string(j, "game_id", cfg.game_id, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.game_id = std::move(*res);
    }

    if (j.contains("rng_seed")) {
      const json &value = j.at("rng_seed");
      if (!value.is_number_integer() && !value.is_number_unsigned())
        return std::unexpected(std::format("game.json 'rng_seed' must be an unsigned integer: {}", path.string()));
      try {
        cfg.rng_seed = value.get<std::uint64_t>();
      } catch (...) {
        return std::unexpected(std::format("game.json 'rng_seed' must be an unsigned integer: {}", path.string()));
      }
    }

    if (j.contains("show_title")) {
      const json &value = j.at("show_title");
      if (!value.is_boolean())
        return std::unexpected(std::format("game.json 'show_title' must be a boolean: {}", path.string()));
      cfg.show_title = value.get<bool>();
    }

    if (auto err = parse_window_settings(j, cfg, path); !err)
      return std::unexpected(err.error());
    if (auto err = parse_gameplay_settings(j, cfg, path); !err)
      return std::unexpected(err.error());
    if (auto err = parse_zoom_settings(j, cfg, path); !err)
      return std::unexpected(err.error());
    if (auto err = parse_resource_paths(j, cfg, path); !err)
      return std::unexpected(err.error());

    {
      auto res = get_nonempty_string(j, "window_title", cfg.window_title, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.window_title = std::move(*res);
    }

    {
      auto res = parse_dialogue_render(j, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.dialogue_render = *res;
    }

    {
      auto res = parse_display_render(j, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.display = *res;
    }

    {
      auto res = parse_player(j, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.player = std::move(*res);
    }

    {
      auto res = parse_starting_flags(j, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.starting_flags = std::move(*res);
    }

    return cfg;
  }

} // namespace corundum::core
