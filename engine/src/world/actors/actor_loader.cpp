// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/direction.hpp>
#include <corundum/world/actors/actor.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::world {

  namespace {

    /// Read a required actor field, or an error naming the field and actor.
    template <typename T>
    std::expected<T, std::string> read_required(const json &entry, std::string_view key, const std::string &path,
                                                std::size_t index) {
      try {
        return entry.at(key).get<T>();
      } catch (...) {
        return std::unexpected(std::format("Spawn points '{}' actors[{}] missing or invalid '{}'", path, index, key));
      }
    }

    /// Read an optional actor field; absent key yields nullopt, wrong type is an error.
    template <typename T>
    std::expected<std::optional<T>, std::string> read_optional(const json &entry, std::string_view key,
                                                               const std::string &path, std::size_t index) {
      if (!entry.contains(key))
        return std::optional<T>{};
      try {
        return entry.at(key).get<T>();
      } catch (...) {
        return std::unexpected(std::format("Spawn points '{}' actors[{}] '{}' has wrong type", path, index, key));
      }
    }

    std::expected<Actor, std::string> parse_actor(const json &entry, const std::string &path, std::size_t index) {
      if (!entry.is_object())
        return std::unexpected(std::format("Spawn points '{}' actors[{}] must be an object", path, index));

      const auto col = read_required<int>(entry, "col", path, index);
      const auto row = read_required<int>(entry, "row", path, index);
      const auto sprite_name = read_required<std::string>(entry, "sprite", path, index);
      const auto dialogue_ref = read_optional<std::string>(entry, "dialogue", path, index);
      const auto facing = read_optional<std::string>(entry, "facing", path, index);
      const auto id = read_optional<std::string>(entry, "id", path, index);

      if (!col)
        return std::unexpected(col.error());
      if (!row)
        return std::unexpected(row.error());
      if (!sprite_name)
        return std::unexpected(sprite_name.error());
      if (!dialogue_ref)
        return std::unexpected(dialogue_ref.error());
      if (!facing)
        return std::unexpected(facing.error());
      if (!id)
        return std::unexpected(id.error());

      if (*col < 0 || *row < 0)
        return std::unexpected(std::format("Spawn points '{}' actors[{}] 'col' and 'row' must be >= 0", path, index));
      if (sprite_name->empty())
        return std::unexpected(std::format("Spawn points '{}' actors[{}] 'sprite' must not be empty", path, index));
      if (*facing && !corundum::core::direction_from_name(**facing))
        return std::unexpected(
            std::format("Spawn points '{}' actors[{}] 'facing' is not a valid direction", path, index));

      Actor actor{
          .col = *col,
          .row = *row,
          .sprite_name = *sprite_name,
          .dialogue_ref = dialogue_ref->value_or(""),
          .id = id->value_or(""),
      };
      if (*facing)
        actor.facing = **facing;
      return actor;
    }

    std::expected<std::vector<Actor>, std::string> parse_actors(const json &j, const std::string &path) {
      if (!j.contains("actors") || !j["actors"].is_array())
        return std::unexpected(std::format("Spawn points '{}' missing 'actors' array", path));

      const auto &arr = j["actors"];
      std::vector<Actor> result;
      result.reserve(arr.size());
      std::unordered_set<std::string> seen_ids;

      for (std::size_t index = 0; index < arr.size(); ++index) {
        auto actor = parse_actor(arr[index], path, index);
        if (!actor)
          return std::unexpected(actor.error());

        if (!actor->id.empty() && !seen_ids.insert(actor->id).second)
          return std::unexpected(
              std::format("Spawn points '{}' actors[{}] duplicate actor id '{}'", path, index, actor->id));

        result.push_back(std::move(*actor));
      }

      return result;
    }

    std::expected<std::optional<PlayerSpawn>, std::string> parse_player_spawn(const json &j, const std::string &path) {
      if (!j.contains("player"))
        return std::optional<PlayerSpawn>{};

      const auto &p = j["player"];
      if (!p.is_object())
        return std::unexpected(std::format("Spawn points '{}' 'player' must be an object", path));

      float col = 0.f;
      float row = 0.f;
      try {
        col = p.at("col").get<float>();
        row = p.at("row").get<float>();
      } catch (...) {
        return std::unexpected(std::format("Spawn points '{}' 'player' missing or invalid 'col'/'row'", path));
      }
      if (col < 0.f || row < 0.f)
        return std::unexpected(std::format("Spawn points '{}' 'player' 'col' and 'row' must be >= 0", path));

      return PlayerSpawn{.col = col, .row = row};
    }

  } // namespace

  std::expected<SpawnPoints, std::string> load_spawn_points(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path))
      return SpawnPoints{};

    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open spawn points '{}'", path.string()));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed spawn points {}: {}", path.string(), e.what()));
    }

    if (!j.is_object())
      return std::unexpected(std::format("Spawn points '{}' must be a JSON object", path.string()));

    SpawnPoints result;

    {
      auto actors_res = parse_actors(j, path.string());
      if (!actors_res)
        return std::unexpected(actors_res.error());
      result.actors = std::move(*actors_res);
    }

    {
      auto player_res = parse_player_spawn(j, path.string());
      if (!player_res)
        return std::unexpected(player_res.error());
      result.player = *player_res;
    }

    return result;
  }

} // namespace corundum::world
