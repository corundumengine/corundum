// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/sprite_atlas.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cmath>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace corundum::sprites {

  namespace {

    /// Record @p message in @p error unless an earlier failure already set it, so a parser can read
    /// every field before reporting the first problem.
    void record_first_error(std::string &error, std::string message) {
      if (error.empty())
        error = std::move(message);
    }

    /// Check a parsed sprite's geometry against itself and the atlas bounds.
    /// @return ok, or the first violation as a message.
    std::expected<void, std::string> validate_sprite(const AtlasSprite &sprite, const std::string &file,
                                                     int atlas_width, int atlas_height) {
      if (sprite.w <= 0 || sprite.h <= 0)
        return std::unexpected(std::format("Sprite atlas '{}' sprite '{}' has non-positive w/h", file, sprite.name));

      const long long right = static_cast<long long>(sprite.x) + sprite.w;
      const long long bottom = static_cast<long long>(sprite.y) + sprite.h;
      if (sprite.x < 0 || sprite.y < 0 || right > atlas_width || bottom > atlas_height)
        return std::unexpected(std::format("Sprite atlas '{}' sprite '{}' lies outside the atlas ({}x{})", file,
                                           sprite.name, atlas_width, atlas_height));

      if (sprite.trim_x < 0 || sprite.trim_y < 0 || sprite.source_width <= 0 || sprite.source_height <= 0)
        return std::unexpected(
            std::format("Sprite atlas '{}' sprite '{}' has invalid trim/source geometry", file, sprite.name));

      const long long trim_right = static_cast<long long>(sprite.trim_x) + sprite.w;
      const long long trim_bottom = static_cast<long long>(sprite.trim_y) + sprite.h;
      if (trim_right > sprite.source_width || trim_bottom > sprite.source_height)
        return std::unexpected(
            std::format("Sprite atlas '{}' sprite '{}' trimmed box exceeds its source frame", file, sprite.name));

      if (!std::isfinite(sprite.pivot_x) || !std::isfinite(sprite.pivot_y))
        return std::unexpected(std::format("Sprite atlas '{}' sprite '{}' has a non-finite pivot", file, sprite.name));

      return {};
    }

    /// Parse one "sprites" entry. `name/x/y/w/h` are required; the trim, source-size, and pivot
    /// fields are optional and default to an untrimmed sprite anchored at its top-left. Values are
    /// type- and range-checked here because nlohmann's `value()`/`get()` throw on a wrong-typed
    /// field, and a malformed atlas must return an error rather than throw out of the loader.
    ///
    /// @param sj The sprite object.
    /// @param file Source file name, for error messages.
    /// @param atlas_width Packed atlas width, used to bound `x + w`.
    /// @param atlas_height Packed atlas height, used to bound `y + h`.
    /// @return The validated sprite, or an error string.
    std::expected<AtlasSprite, std::string> parse_sprite(const json &sj, const std::string &file, int atlas_width,
                                                         int atlas_height) {
      if (!sj.is_object())
        return std::unexpected(std::format("Sprite atlas '{}' has a non-object sprite entry", file));

      if (!sj.contains("name") || !sj["name"].is_string())
        return std::unexpected(
            std::format("Sprite atlas '{}' has a sprite entry with a missing or non-string 'name'", file));
      const std::string name = sj["name"].get<std::string>();
      if (name.empty())
        return std::unexpected(std::format("Sprite atlas '{}' has a sprite with empty 'name'", file));

      // Read every field without branching per field; the first type error is reported below.
      std::string error;
      const auto required_int = [&](const char *key) -> int {
        if (!sj.contains(key) || !sj[key].is_number_integer()) {
          record_first_error(
              error, std::format("Sprite atlas '{}' sprite '{}' has a missing or non-integer '{}'", file, name, key));
          return 0;
        }
        return sj[key].get<int>();
      };
      const auto optional_int = [&](const char *key, int fallback) -> int {
        if (!sj.contains(key))
          return fallback;
        if (!sj[key].is_number_integer()) {
          record_first_error(
              error, std::format("Sprite atlas '{}' sprite '{}' field '{}' must be an integer", file, name, key));
          return fallback;
        }
        return sj[key].get<int>();
      };
      const auto optional_float = [&](const char *key, float fallback) -> float {
        if (!sj.contains(key))
          return fallback;
        if (!sj[key].is_number()) {
          record_first_error(error,
                             std::format("Sprite atlas '{}' sprite '{}' field '{}' must be a number", file, name, key));
          return fallback;
        }
        return sj[key].get<float>();
      };

      AtlasSprite sprite;
      sprite.name = name;
      sprite.x = required_int("x");
      sprite.y = required_int("y");
      sprite.w = required_int("w");
      sprite.h = required_int("h");
      sprite.trim_x = optional_int("trim_x", 0);
      sprite.trim_y = optional_int("trim_y", 0);
      sprite.source_width = optional_int("source_width", sprite.w);
      sprite.source_height = optional_int("source_height", sprite.h);
      sprite.pivot_x = optional_float("pivot_x", 0.f);
      sprite.pivot_y = optional_float("pivot_y", 0.f);

      if (!error.empty())
        return std::unexpected(std::move(error));

      if (auto valid = validate_sprite(sprite, file, atlas_width, atlas_height); !valid)
        return std::unexpected(std::move(valid).error());

      return sprite;
    }

    /// Parse and validate the "sprites" array, rejecting duplicate names.
    std::expected<std::vector<AtlasSprite>, std::string> parse_sprites(const json &root, const std::string &file,
                                                                       int atlas_width, int atlas_height) {
      if (!root.contains("sprites") || !root["sprites"].is_array())
        return std::unexpected(std::format("Sprite atlas '{}' missing 'sprites' array", file));

      std::vector<AtlasSprite> sprites;
      std::unordered_set<std::string> seen_names;
      sprites.reserve(root["sprites"].size());

      for (const auto &sj : root["sprites"]) {
        auto sprite = parse_sprite(sj, file, atlas_width, atlas_height);
        if (!sprite)
          return std::unexpected(std::move(sprite).error());

        if (!seen_names.insert(sprite->name).second)
          return std::unexpected(std::format("Sprite atlas '{}' has duplicate sprite name '{}'", file, sprite->name));

        sprites.push_back(std::move(*sprite));
      }

      return sprites;
    }

    /// Parse the required, non-empty string field @p key from @p root.
    std::expected<std::string, std::string> require_string(const json &root, const std::string &file, const char *key) {
      if (!root.contains(key))
        return std::unexpected(std::format("Sprite atlas '{}' missing '{}'", file, key));
      if (!root[key].is_string())
        return std::unexpected(std::format("Sprite atlas '{}' field '{}' must be a string", file, key));

      std::string value = root[key].get<std::string>();
      if (value.empty())
        return std::unexpected(std::format("Sprite atlas '{}' field '{}' must not be empty", file, key));
      return value;
    }

    /// Parse the required, positive integer field @p key from @p root.
    std::expected<int, std::string> require_positive_int(const json &root, const std::string &file, const char *key) {
      if (!root.contains(key))
        return std::unexpected(std::format("Sprite atlas '{}' missing '{}'", file, key));
      if (!root[key].is_number_integer())
        return std::unexpected(std::format("Sprite atlas '{}' field '{}' must be an integer", file, key));

      const int value = root[key].get<int>();
      if (value <= 0)
        return std::unexpected(std::format("Sprite atlas '{}' field '{}' must be positive", file, key));
      return value;
    }

    /// Hard-fail on a missing or mismatched schema_version rather than routing through
    /// core::prepare_schema_version: atlases have no legacy absent-version form, so "absent" is an
    /// error here, not an implicit version 1.
    std::expected<void, std::string> check_schema_version(const json &root, const std::string &file) {
      if (!root.contains("schema_version") || !root["schema_version"].is_number_integer())
        return std::unexpected(std::format("Sprite atlas '{}' has a missing or non-integer 'schema_version'", file));

      const int schema_version = root["schema_version"].get<int>();
      if (schema_version != k_sprite_atlas_schema_version)
        return std::unexpected(
            std::format("Sprite atlas '{}' has schema_version {}, but this engine expects {} — regenerate with a "
                        "matching spritepacker version",
                        file, schema_version, k_sprite_atlas_schema_version));
      return {};
    }

    /// Parse the optional "pivot_basis" field, defaulting to PivotBasis::TrimmedTopOrigin when
    /// absent.
    std::expected<PivotBasis, std::string> parse_pivot_basis(const json &root, const std::string &file) {
      if (!root.contains("pivot_basis"))
        return PivotBasis::TrimmedTopOrigin;
      if (!root["pivot_basis"].is_string())
        return std::unexpected(std::format("Sprite atlas '{}' field 'pivot_basis' must be a string", file));

      const std::string basis = root["pivot_basis"].get<std::string>();
      if (basis == "full")
        return PivotBasis::FullCanvasBottomOrigin;
      if (basis == "trimmed")
        return PivotBasis::TrimmedTopOrigin;

      return std::unexpected(
          std::format("Sprite atlas '{}' field 'pivot_basis' must be 'trimmed' or 'full', got '{}'", file, basis));
    }

  } // namespace

  std::expected<SpriteAtlas, std::string> load_sprite_atlas(const fs::path &path) {
    const std::string file = path.string();

    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open sprite atlas: {}", file));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed sprite atlas {}: {}", file, e.what()));
    }

    if (!j.is_object())
      return std::unexpected(std::format("Sprite atlas '{}' must be a JSON object", file));

    if (auto version = check_schema_version(j, file); !version)
      return std::unexpected(std::move(version).error());

    auto atlas_path = require_string(j, file, "path");
    if (!atlas_path)
      return std::unexpected(std::move(atlas_path).error());

    auto width = require_positive_int(j, file, "width");
    if (!width)
      return std::unexpected(std::move(width).error());

    auto height = require_positive_int(j, file, "height");
    if (!height)
      return std::unexpected(std::move(height).error());

    auto pivot_basis = parse_pivot_basis(j, file);
    if (!pivot_basis)
      return std::unexpected(std::move(pivot_basis).error());

    auto sprites = parse_sprites(j, file, *width, *height);
    if (!sprites)
      return std::unexpected(std::move(sprites).error());

    return SpriteAtlas{
        .path = std::move(*atlas_path),
        .width = *width,
        .height = *height,
        .pivot_basis = *pivot_basis,
        .sprites = std::move(*sprites),
    };
  }

  PivotPoint resolve_pivot(const AtlasSprite &sprite, PivotBasis basis) noexcept {
    if (basis == PivotBasis::FullCanvasBottomOrigin)
      return {.x = sprite.pivot_x, .y = sprite.pivot_y};

    // Trimmed-box pivot: a fraction of the trimmed w/h with y measured from the top. Recover the
    // full-frame position by adding the trim offset, then flip y into the bottom-origin convention.
    const float pivot_x_full = static_cast<float>(sprite.trim_x) + (sprite.pivot_x * static_cast<float>(sprite.w));
    const float pivot_y_full_raster =
        static_cast<float>(sprite.trim_y) + (sprite.pivot_y * static_cast<float>(sprite.h));

    // @pre guarantees positive source dimensions; the guards keep a hand-built sprite safe.
    const float x = sprite.source_width > 0 ? pivot_x_full / static_cast<float>(sprite.source_width) : 0.5f;
    const float y_raster =
        sprite.source_height > 0 ? pivot_y_full_raster / static_cast<float>(sprite.source_height) : 1.f;
    return {.x = x, .y = 1.f - y_raster};
  }

} // namespace corundum::sprites
