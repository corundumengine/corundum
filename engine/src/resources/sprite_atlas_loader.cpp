#include <corundum/resources/sprite_atlas.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::resources {

  std::expected<SpriteAtlas, std::string> load_sprite_atlas(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open sprite atlas: {}", path.string()));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed sprite atlas {}: {}", path.string(), e.what()));
    }

    auto require_str = [&j, &path](const char *key) -> std::expected<std::string, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sprite atlas '{}' missing '{}'", path.string(), key));
      try {
        return j[key].get<std::string>();
      } catch (const nlohmann::json::exception &e) {
        return std::unexpected(std::format("Sprite atlas field '{}' has wrong type: {}", key, e.what()));
      }
    };

    auto require_pos_int = [&j, &path](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Sprite atlas '{}' missing '{}'", path.string(), key));
      int v;
      try {
        v = j[key].get<int>();
      } catch (const nlohmann::json::exception &e) {
        return std::unexpected(std::format("Sprite atlas field '{}' has wrong type: {}", key, e.what()));
      }
      if (v <= 0)
        return std::unexpected(std::format("Sprite atlas field '{}' must be positive", key));
      return v;
    };

    if (!j.contains("schema_version"))
      return std::unexpected(std::format("Sprite atlas '{}' missing 'schema_version'", path.string()));
    if (!j["schema_version"].is_number_integer())
      return std::unexpected(std::format("Sprite atlas '{}' field 'schema_version' has wrong type", path.string()));
    const int schema_version = j["schema_version"].get<int>();
    if (schema_version != k_sprite_atlas_schema_version)
      return std::unexpected(
          std::format("Sprite atlas '{}' has schema_version {}, but this engine expects {} — regenerate with a "
                      "matching spritepacker version",
                      path.string(), schema_version, k_sprite_atlas_schema_version));

    SpriteAtlas atlas;
    {
      auto r = require_str("path");
      if (!r)
        return std::unexpected(std::move(r.error()));
      atlas.path = std::move(*r);
    }
    {
      auto r = require_pos_int("width");
      if (!r)
        return std::unexpected(std::move(r.error()));
      atlas.width = *r;
    }
    {
      auto r = require_pos_int("height");
      if (!r)
        return std::unexpected(std::move(r.error()));
      atlas.height = *r;
    }

    if (!j.contains("sprites") || !j["sprites"].is_array())
      return std::unexpected(std::format("Sprite atlas '{}' missing 'sprites' array", path.string()));

    std::unordered_set<std::string> seen_names;
    atlas.sprites.reserve(j["sprites"].size());

    for (const auto &sj : j["sprites"]) {
      AtlasSprite sprite;
      try {
        sprite.name = sj.at("name").get<std::string>();
        sprite.x = sj.at("x").get<int>();
        sprite.y = sj.at("y").get<int>();
        sprite.w = sj.at("w").get<int>();
        sprite.h = sj.at("h").get<int>();
      } catch (const nlohmann::json::exception &) {
        return std::unexpected(
            std::format("Sprite atlas '{}' has a sprite entry missing required field (name/x/y/w/h)", path.string()));
      }

      if (sprite.name.empty())
        return std::unexpected(std::format("Sprite atlas '{}' has a sprite with empty 'name'", path.string()));
      if (sprite.w <= 0 || sprite.h <= 0)
        return std::unexpected(
            std::format("Sprite atlas '{}' sprite '{}' has non-positive w/h", path.string(), sprite.name));
      if (!seen_names.insert(sprite.name).second)
        return std::unexpected(
            std::format("Sprite atlas '{}' has duplicate sprite name '{}'", path.string(), sprite.name));

      sprite.trim_x = sj.value("trim_x", 0);
      sprite.trim_y = sj.value("trim_y", 0);
      sprite.source_width = sj.value("source_width", sprite.w);
      sprite.source_height = sj.value("source_height", sprite.h);
      sprite.pivot_x = sj.value("pivot_x", 0.f);
      sprite.pivot_y = sj.value("pivot_y", 0.f);

      atlas.sprites.push_back(std::move(sprite));
    }

    return atlas;
  }

} // namespace corundum::resources
