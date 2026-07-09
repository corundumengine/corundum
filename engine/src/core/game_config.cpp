#include <corundum/core/game_config.hpp>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::core {

  namespace {

    std::expected<unsigned int, std::string> get_positive_unsigned(const json &j, const std::string &key,
                                                                   unsigned int default_val, const fs::path &path) {
      if (!j.contains(key))
        return default_val;
      int v;
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
      float v;
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

      auto get_uint = [&](const std::string &key, unsigned default_val) -> std::expected<unsigned, std::string> {
        if (!sub.contains(key))
          return default_val;
        unsigned v;
        try {
          v = sub.at(key).get<unsigned>();
        } catch (...) {
          return std::unexpected(std::format("game.json 'dialogue_render.{}' has wrong type: {}", key, path.string()));
        }
        return v;
      };

      auto get_pos_float = [&](const std::string &key, float default_val) -> std::expected<float, std::string> {
        if (!sub.contains(key))
          return default_val;
        float v;
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
        float frac;
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

  } // namespace

  std::expected<GameConfig, std::string> load_game_config(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open game config: {}", path.string()));

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed game.json {}: {}", path.string(), e.what()));
    }

    if (!j.is_object())
      return std::unexpected(std::format("game.json must be a JSON object: {}", path.string()));

    GameConfig cfg;

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

    if (j.contains("framerate")) {
      unsigned fr;
      try {
        fr = j.at("framerate").get<unsigned>();
      } catch (...) {
        return std::unexpected(std::format("game.json 'framerate' has wrong type: {}", path.string()));
      }
      if (fr == 0)
        return std::unexpected(std::format("game.json 'framerate' must be > 0: {}", path.string()));
      cfg.framerate = fr;
    }

    {
      auto res = get_nonempty_string(j, "font_dir", cfg.paths.font_dir, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.paths.font_dir = std::move(*res);
    }
    {
      auto res = get_nonempty_string(j, "game_font", cfg.paths.game_font, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.paths.game_font = std::move(*res);
    }
    {
      auto res = get_nonempty_string(j, "ui_font", cfg.paths.ui_font, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.paths.ui_font = std::move(*res);
    }
    {
      auto res = get_nonempty_string(j, "icons_font", cfg.paths.icons_font, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.paths.icons_font = std::move(*res);
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
      auto res = get_nonempty_string(j, "quests_dir", cfg.paths.quests_dir, path);
      if (!res)
        return std::unexpected(res.error());
      cfg.paths.quests_dir = std::move(*res);
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

    if (j.contains("vsync")) {
      try {
        cfg.vsync = j.at("vsync").get<bool>();
      } catch (...) {
        return std::unexpected(std::format("game.json 'vsync' must be a boolean: {}", path.string()));
      }
    }

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
      cfg.dialogue_render = std::move(*res);
    }

    return cfg;
  }

} // namespace corundum::core
