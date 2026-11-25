// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cmath>
#include <corundum/core/content_source.hpp>
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

/** Upper bound on the configured simulation rate. Real rates top out in the low hundreds, so
 *  anything past this is a corrupt or hand-edited config.
 */
constexpr unsigned int k_max_simulation_fps{1000};

std::expected<unsigned int, std::string> get_positive_unsigned(const json &j, const std::string &key,
                                                               unsigned int default_val, const fs::path &path) {
  if (!j.contains(key))
    return default_val;
  const json &value = j.at(key);
  if (!value.is_number_integer())
    return std::unexpected(std::format("game.json '{}' must be an integer: {}", key, path.string()));
  int v{0};
  try {
    v = value.get<int>();
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

/** Assign `parsed` to `target`, or forward the error that stopped it. */
template <typename T> std::expected<void, std::string> read_into(std::expected<T, std::string> parsed, T &target) {
  if (!parsed)
    return std::unexpected(std::move(parsed).error());
  target = std::move(*parsed);
  return {};
}

/** Read `key` from the `section` object as a number, or `default_val` when absent. */
template <typename T>
std::expected<T, std::string> read_section_number(const json &section, std::string_view section_name,
                                                  std::string_view key, T default_val, const fs::path &path) {
  if (!section.contains(key))
    return default_val;
  try {
    return section.at(key).get<T>();
  } catch (...) {
    return std::unexpected(std::format("game.json '{}.{}' has wrong type: {}", section_name, key, path.string()));
  }
}

/** Read `key` from the `section` object as a non-empty string, or `default_val` when absent. */
std::expected<std::string, std::string> read_section_string(const json &section, std::string_view section_name,
                                                            std::string_view key, const std::string &default_val,
                                                            const fs::path &path) {
  if (!section.contains(key))
    return default_val;
  std::string value;
  try {
    value = section.at(key).get<std::string>();
  } catch (...) {
    return std::unexpected(std::format("game.json '{}.{}' has wrong type: {}", section_name, key, path.string()));
  }
  if (value.empty())
    return std::unexpected(std::format("game.json '{}.{}' must not be empty: {}", section_name, key, path.string()));
  return value;
}

/** Read `key` from the `section` object as a float >= 0, or `default_val` when absent. */
std::expected<float, std::string> read_section_non_negative_float(const json &section, std::string_view section_name,
                                                                  std::string_view key, float default_val,
                                                                  const fs::path &path) {
  if (!section.contains(key))
    return default_val;
  float value{NAN};
  try {
    value = section.at(key).get<float>();
  } catch (...) {
    return std::unexpected(std::format("game.json '{}.{}' has wrong type: {}", section_name, key, path.string()));
  }
  if (value < 0.f)
    return std::unexpected(std::format("game.json '{}.{}' must be >= 0: {}", section_name, key, path.string()));
  return value;
}

std::expected<unsigned, std::string> read_simulation_fps(const json &j, unsigned default_val, const fs::path &path) {
  if (!j.contains("simulation_fps"))
    return default_val;
  unsigned frames_per_second{0};
  try {
    frames_per_second = j.at("simulation_fps").get<unsigned>();
  } catch (...) {
    return std::unexpected(std::format("game.json 'simulation_fps' has wrong type: {}", path.string()));
  }
  if (frames_per_second == 0)
    return std::unexpected(std::format("game.json 'simulation_fps' must be > 0: {}", path.string()));
  if (frames_per_second > k_max_simulation_fps)
    return std::unexpected(
        std::format("game.json 'simulation_fps' must be <= {}: {}", k_max_simulation_fps, path.string()));
  return frames_per_second;
}

std::expected<bool, std::string> read_bool_field(const json &j, std::string_view key, bool default_val,
                                                 const fs::path &path) {
  if (!j.contains(key))
    return default_val;
  try {
    return j.at(key).get<bool>();
  } catch (...) {
    return std::unexpected(std::format("game.json '{}' must be a boolean: {}", key, path.string()));
  }
}

std::expected<WindowMode, std::string> read_window_mode(const json &j, WindowMode default_val, const fs::path &path) {
  if (!j.contains("window_mode"))
    return default_val;
  const json &value = j.at("window_mode");
  const std::optional<WindowMode> mode =
      value.is_string() ? core::parse_window_mode(value.get<std::string>()) : std::nullopt;
  if (!mode)
    return std::unexpected(
        std::format(R"(game.json 'window_mode' must be "windowed" or "fullscreen": {})", path.string()));
  return *mode;
}

/** Assign the string at `key` to `target` when present; `target` keeps its default otherwise. */
std::expected<void, std::string> read_optional_string_field(const json &j, std::string_view key, std::string &target,
                                                            const fs::path &path) {
  if (!j.contains(key))
    return {};
  try {
    target = j.at(key).get<std::string>();
  } catch (...) {
    return std::unexpected(std::format("game.json '{}' has wrong type: {}", key, path.string()));
  }
  return {};
}

std::expected<DialogueRenderConfig, std::string> parse_dialogue_render(const json &j, const fs::path &path) {
  DialogueRenderConfig dr;
  if (!j.contains("dialogue_render"))
    return dr;

  const auto &sub = j.at("dialogue_render");
  if (!sub.is_object())
    return std::unexpected(std::format("game.json 'dialogue_render' must be an object: {}", path.string()));

  if (auto res = read_into(
          read_section_number<unsigned>(sub, "dialogue_render", "font_size_speaker", dr.font_size_speaker, path),
          dr.font_size_speaker);
      !res)
    return std::unexpected(res.error());
  if (auto res =
          read_into(read_section_number<unsigned>(sub, "dialogue_render", "font_size_body", dr.font_size_body, path),
                    dr.font_size_body);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(
          read_section_number<unsigned>(sub, "dialogue_render", "font_size_prompt", dr.font_size_prompt, path),
          dr.font_size_prompt);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_section_number<float>(sub, "dialogue_render", "margin", dr.margin, path), dr.margin);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_section_number<float>(sub, "dialogue_render", "line_spacing", dr.line_spacing, path),
                           dr.line_spacing);
      !res)
    return std::unexpected(res.error());

  if (sub.contains("panel_height_frac")) {
    float fraction{NAN};
    try {
      fraction = sub.at("panel_height_frac").get<float>();
    } catch (...) {
      return std::unexpected(
          std::format("game.json 'dialogue_render.panel_height_frac' has wrong type: {}", path.string()));
    }
    if (fraction <= 0.f || fraction >= 1.f)
      return std::unexpected(
          std::format("game.json 'dialogue_render.panel_height_frac' must be in (0, 1): {}", path.string()));
    dr.panel_height_frac = fraction;
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

  if (auto res = read_into(read_section_string(sub, "player", "walk_sprite", pc.walk_sprite, path), pc.walk_sprite);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_section_string(sub, "player", "idle_sprite", pc.idle_sprite, path), pc.idle_sprite);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_section_non_negative_float(sub, "player", "col", pc.col, path), pc.col); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_section_non_negative_float(sub, "player", "row", pc.row, path), pc.row); !res)
    return std::unexpected(res.error());

  return pc;
}

std::expected<void, std::string> parse_framing_settings(const json &j, GameConfig &config, const fs::path &path) {
  if (j.contains("show_title")) {
    const json &value = j.at("show_title");
    if (!value.is_boolean())
      return std::unexpected(std::format("game.json 'show_title' must be a boolean: {}", path.string()));
    config.show_title = value.get<bool>();
  }

  if (!j.contains("title_music"))
    return {};
  const json &block = j.at("title_music");
  const bool has_track = block.is_object() && block.contains("track") && block.at("track").is_string() &&
                         !block.at("track").get_ref<const std::string &>().empty();
  if (!has_track)
    return std::unexpected(
        std::format("game.json 'title_music' must be an object with a non-empty 'track' path: {}", path.string()));
  config.title_music.track = block.at("track").get<std::string>();

  if (!block.contains("loop_delay_seconds"))
    return {};
  const json &delay = block.at("loop_delay_seconds");
  if (!delay.is_number() || delay.get<float>() < 0.f)
    return std::unexpected(
        std::format("game.json 'title_music.loop_delay_seconds' must be a number >= 0: {}", path.string()));
  config.title_music.loop_delay_seconds = delay.get<float>();
  return {};
}

std::expected<void, std::string> parse_window_settings(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res = read_into(get_positive_unsigned(j, "win_w", config.win_w, path), config.win_w); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_unsigned(j, "win_h", config.win_h, path), config.win_h); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_simulation_fps(j, config.simulation_fps, path), config.simulation_fps); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_bool_field(j, "vsync", config.vsync, path), config.vsync); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_window_mode(j, config.window_mode, path), config.window_mode); !res)
    return std::unexpected(res.error());
  return {};
}

std::expected<void, std::string> parse_gameplay_settings(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res =
          read_into(get_positive_float(j, "interact_radius", config.interact_radius, path), config.interact_radius);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_float(j, "player_speed", config.player_speed, path), config.player_speed); !res)
    return std::unexpected(res.error());
  if (auto res =
          read_into(get_positive_float(j, "character_scale", config.character_scale, path), config.character_scale);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_float(j, "tile_scale", config.tile_scale, path), config.tile_scale); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_float(j, "elevation_step_px", config.elevation_step_px, path),
                           config.elevation_step_px);
      !res)
    return std::unexpected(res.error());
  if (auto res =
          read_into(get_positive_unsigned(j, "max_step_height", config.max_step_height, path), config.max_step_height);
      !res)
    return std::unexpected(res.error());
  return {};
}

std::expected<void, std::string> parse_zoom_settings(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res = read_into(get_positive_float(j, "min_zoom", config.min_zoom, path), config.min_zoom); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_float(j, "max_zoom", config.max_zoom, path), config.max_zoom); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_positive_float(j, "default_zoom", config.default_zoom, path), config.default_zoom); !res)
    return std::unexpected(res.error());
  if (config.min_zoom > config.max_zoom)
    return std::unexpected(std::format("game.json 'min_zoom' must be <= 'max_zoom': {}", path.string()));
  return {};
}

/** Read one style file name of a font family; an optional style may be absent. */
std::expected<std::string, std::string> read_font_style(const json &family, std::string_view prefix,
                                                        std::string_view style, bool required, const fs::path &path) {
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
}

std::expected<ui::FontFamilyPaths, std::string> parse_font_family(const json &fonts, std::string_view role,
                                                                  const fs::path &path) {
  const std::string prefix = std::format("fonts.{}", role);
  if (!fonts.contains(role))
    return std::unexpected(std::format("game.json '{}' is required: {}", prefix, path.string()));
  const json &family = fonts.at(role);
  if (!family.is_object())
    return std::unexpected(std::format("game.json '{}' must be an object: {}", prefix, path.string()));

  ui::FontFamilyPaths out;
  if (auto res = read_into(read_font_style(family, prefix, "regular", /*required=*/true, path), out.regular); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_font_style(family, prefix, "bold", /*required=*/false, path), out.bold); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_font_style(family, prefix, "bold_italic", /*required=*/false, path), out.bold_italic);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(read_font_style(family, prefix, "italic", /*required=*/false, path), out.italic); !res)
    return std::unexpected(res.error());
  return out;
}

std::expected<void, std::string> parse_fonts(const json &j, GameConfig &config, const fs::path &path) {
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
    config.paths.fonts[static_cast<std::size_t>(role)] = std::move(*family);
  }

  // The display family is optional; a game without one draws display text in the ui family.
  const auto display_slot = static_cast<std::size_t>(ui::FontRole::Display);
  if (!fonts.contains("display")) {
    config.paths.fonts[display_slot] = config.paths.fonts[static_cast<std::size_t>(ui::FontRole::Ui)];
    return {};
  }

  auto family = parse_font_family(fonts, "display", path);
  if (!family)
    return std::unexpected(family.error());
  config.paths.fonts[display_slot] = std::move(*family);
  return {};
}

/** Read every named resource path in `j`, leaving the field's default when the key is absent. */
std::expected<void, std::string> parse_path_fields(const json &j, ResourcePaths &paths, const fs::path &path) {
  struct Field {
    std::string_view key;
    std::string ResourcePaths::*target;
  };

  static constexpr std::array k_fields{
      Field{.key = "codex_dir", .target = &ResourcePaths::codex_dir},
      Field{.key = "dialogue_dir", .target = &ResourcePaths::dialogue_dir},
      Field{.key = "font_dir", .target = &ResourcePaths::font_dir},
      Field{.key = "items_dir", .target = &ResourcePaths::items_dir},
      Field{.key = "locations_dir", .target = &ResourcePaths::locations_dir},
      Field{.key = "portals_dir", .target = &ResourcePaths::portals_dir},
      Field{.key = "quests_dir", .target = &ResourcePaths::quests_dir},
      Field{.key = "shops_dir", .target = &ResourcePaths::shops_dir},
      Field{.key = "sounds_dir", .target = &ResourcePaths::sounds_dir},
      Field{.key = "spawn_points_dir", .target = &ResourcePaths::spawn_points_dir},
      Field{.key = "sprites_dir", .target = &ResourcePaths::sprites_dir},
      Field{.key = "tilemap_path", .target = &ResourcePaths::tilemap_path},
      Field{.key = "ui_borders_file", .target = &ResourcePaths::ui_borders_file},
  };
  for (const Field &field : k_fields) {
    auto res = get_nonempty_string(j, std::string{field.key}, paths.*field.target, path);
    if (!res)
      return std::unexpected(res.error());
    paths.*field.target = std::move(*res);
  }
  return {};
}

std::expected<void, std::string> parse_splash_images(const json &j, ResourcePaths &paths, const fs::path &path) {
  if (!j.contains("splash_images"))
    return {};
  const json &images = j.at("splash_images");
  if (!images.is_array() || images.size() > k_max_splash_images)
    return std::unexpected(std::format("game.json 'splash_images' must be an array of at most {} paths: {}",
                                       k_max_splash_images, path.string()));
  for (const json &image : images) {
    std::string splash_path = image.is_string() ? image.get<std::string>() : std::string{};
    if (splash_path.empty())
      return std::unexpected(
          std::format("game.json 'splash_images' entries must be non-empty strings: {}", path.string()));
    paths.splash_images.push_back(std::move(splash_path));
  }
  return {};
}

/** Read the free-form resource paths (no non-empty rule) and the splash image list. */
std::expected<void, std::string> parse_free_paths(const json &j, ResourcePaths &paths, const fs::path &path) {
  if (auto res = read_optional_string_field(j, "credits_file", paths.credits_file, path); !res)
    return std::unexpected(res.error());
  if (auto res = read_optional_string_field(j, "sounds_catalog", paths.sounds_catalog, path); !res)
    return std::unexpected(res.error());
  if (auto res = read_optional_string_field(j, "world_manifest_path", paths.world_manifest_path, path); !res)
    return std::unexpected(res.error());
  return parse_splash_images(j, paths, path);
}

std::expected<void, std::string> parse_resource_paths(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res = parse_path_fields(j, config.paths, path); !res)
    return std::unexpected(res.error());
  if (auto res = parse_fonts(j, config, path); !res)
    return std::unexpected(res.error());
  if (auto res = parse_free_paths(j, config.paths, path); !res)
    return std::unexpected(res.error());
  return {};
}

std::expected<std::flat_map<std::string, int>, std::string> parse_starting_flags(const json &j, const fs::path &path) {
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
      return std::unexpected(std::format("game.json 'starting_flags.{}' must be an integer: {}", key, path.string()));
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

std::expected<void, std::string> parse_rng_seed(const json &j, GameConfig &config, const fs::path &path) {
  if (!j.contains("rng_seed"))
    return {};
  const json &value = j.at("rng_seed");
  if (!value.is_number_integer() && !value.is_number_unsigned())
    return std::unexpected(std::format("game.json 'rng_seed' must be an unsigned integer: {}", path.string()));
  try {
    config.rng_seed = value.get<std::uint64_t>();
  } catch (...) {
    return std::unexpected(std::format("game.json 'rng_seed' must be an unsigned integer: {}", path.string()));
  }
  return {};
}

std::expected<void, std::string> parse_settings(const json &j, GameConfig &config, const fs::path &path) {
  if (auto err = parse_framing_settings(j, config, path); !err)
    return std::unexpected(err.error());
  if (auto err = parse_window_settings(j, config, path); !err)
    return std::unexpected(err.error());
  if (auto err = parse_gameplay_settings(j, config, path); !err)
    return std::unexpected(err.error());
  if (auto err = parse_zoom_settings(j, config, path); !err)
    return std::unexpected(err.error());
  if (auto err = parse_resource_paths(j, config, path); !err)
    return std::unexpected(err.error());
  return {};
}

std::expected<void, std::string> parse_titles(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res = read_into(get_nonempty_string(j, "window_title", config.window_title, path), config.window_title);
      !res)
    return std::unexpected(res.error());
  if (auto res = read_into(get_nonempty_string(j, "title", config.title, path), config.title); !res)
    return std::unexpected(res.error());

  // An unset title falls back to the window title, then to the engine's own name; an unset
  // window title falls back to the title. A loaded config therefore always carries both.
  if (config.title.empty())
    config.title = config.window_title;
  if (config.title.empty())
    config.title = std::string{k_default_game_title};
  if (config.window_title.empty())
    config.window_title = config.title;
  return {};
}

std::expected<void, std::string> parse_optional_configs(const json &j, GameConfig &config, const fs::path &path) {
  if (auto res = read_into(parse_dialogue_render(j, path), config.dialogue_render); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(parse_display_render(j, path), config.display); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(parse_player(j, path), config.player); !res)
    return std::unexpected(res.error());
  if (auto res = read_into(parse_starting_flags(j, path), config.starting_flags); !res)
    return std::unexpected(res.error());
  return {};
}

} // namespace

std::expected<GameConfig, std::string> load_game_config(const core::ContentSource &content, const fs::path &path) {
  auto j_result = read_json(content, path, "game.json");
  if (!j_result)
    return std::unexpected(std::move(j_result).error());
  json j = std::move(*j_result);

  if (!j.is_object())
    return std::unexpected(std::format("game.json must be a JSON object: {}", path.string()));

  GameConfig config;
  if (auto res = read_into(get_nonempty_string(j, "game_id", config.game_id, path), config.game_id); !res)
    return std::unexpected(res.error());
  if (auto res = parse_rng_seed(j, config, path); !res)
    return std::unexpected(res.error());
  if (auto res = parse_settings(j, config, path); !res)
    return std::unexpected(res.error());
  if (auto res = parse_titles(j, config, path); !res)
    return std::unexpected(res.error());
  if (auto res = parse_optional_configs(j, config, path); !res)
    return std::unexpected(res.error());
  return config;
}

std::string_view game_title(const GameConfig &config) noexcept {
  if (!config.title.empty())
    return config.title;
  if (!config.window_title.empty())
    return config.window_title;
  return k_default_game_title;
}

} // namespace corundum::core
