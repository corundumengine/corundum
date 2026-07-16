#include <corundum/gameplay/world/actors/actor.hpp>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::gameplay::world {

  namespace {

    std::expected<std::vector<Actor>, std::string> parse_actors(const json &j, const std::string &path) {
      if (!j.contains("actors") || !j["actors"].is_array())
        return std::unexpected(std::format("Spawn points '{}' missing 'actors' array", path));

      const auto &arr = j["actors"];
      std::vector<Actor> result;
      result.reserve(arr.size());

      for (std::size_t i = 0; i < arr.size(); ++i) {
        const auto &entry = arr[i];

        if (!entry.is_object())
          return std::unexpected(std::format("Spawn points '{}' actors[{}] must be an object", path, i));

        int col, row;
        try {
          col = entry.at("col").get<int>();
          row = entry.at("row").get<int>();
        } catch (...) {
          return std::unexpected(std::format("Spawn points '{}' actors[{}] missing or invalid 'col'/'row'", path, i));
        }
        if (col < 0 || row < 0)
          return std::unexpected(std::format("Spawn points '{}' actors[{}] 'col' and 'row' must be >= 0", path, i));

        std::string sprite_name;
        try {
          sprite_name = entry.at("sprite").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Spawn points '{}' actors[{}] missing or invalid 'sprite'", path, i));
        }
        if (sprite_name.empty())
          return std::unexpected(std::format("Spawn points '{}' actors[{}] 'sprite' must not be empty", path, i));

        std::string dialogue_ref;
        if (entry.contains("dialogue")) {
          try {
            dialogue_ref = entry.at("dialogue").get<std::string>();
          } catch (...) {
            return std::unexpected(std::format("Spawn points '{}' actors[{}] 'dialogue' has wrong type", path, i));
          }
        }

        std::string facing = "south";
        if (entry.contains("facing")) {
          try {
            facing = entry.at("facing").get<std::string>();
          } catch (...) {
            return std::unexpected(std::format("Spawn points '{}' actors[{}] 'facing' has wrong type", path, i));
          }
        }

        result.push_back(Actor{col, row, std::move(sprite_name), std::move(dialogue_ref), std::move(facing)});
      }

      return result;
    }

    std::expected<std::optional<PlayerSpawn>, std::string> parse_player_spawn(const json &j, const std::string &path) {
      if (!j.contains("player"))
        return std::optional<PlayerSpawn>{};

      const auto &p = j["player"];
      if (!p.is_object())
        return std::unexpected(std::format("Spawn points '{}' 'player' must be an object", path));

      float col, row;
      try {
        col = p.at("col").get<float>();
        row = p.at("row").get<float>();
      } catch (...) {
        return std::unexpected(std::format("Spawn points '{}' 'player' missing or invalid 'col'/'row'", path));
      }
      if (col < 0.f || row < 0.f)
        return std::unexpected(std::format("Spawn points '{}' 'player' 'col' and 'row' must be >= 0", path));

      return PlayerSpawn{col, row};
    }

  } // namespace

  std::expected<SpawnPoints, std::string> load_spawn_points(const std::string &path) {
    std::ifstream f(path);
    if (!f)
      return SpawnPoints{};

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed spawn points {}: {}", path, e.what()));
    }

    if (!j.is_object())
      return std::unexpected(std::format("Spawn points '{}' must be a JSON object", path));

    SpawnPoints result;

    {
      auto actors_res = parse_actors(j, path);
      if (!actors_res)
        return std::unexpected(actors_res.error());
      result.actors = std::move(*actors_res);
    }

    {
      auto player_res = parse_player_spawn(j, path);
      if (!player_res)
        return std::unexpected(player_res.error());
      result.player = std::move(*player_res);
    }

    return result;
  }

  std::expected<std::vector<Actor>, std::string> load_actors(const std::string &path) {
    auto res = load_spawn_points(path);
    if (!res)
      return std::unexpected(res.error());
    return std::move(res->actors);
  }

} // namespace corundum::gameplay::world
