// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/content_source.hpp>
#include <corundum/core/direction.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/entities/tables/actor_id_table.hpp>
#include <corundum/entities/tables/dialogue_table.hpp>
#include <corundum/world/actors/actor.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
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

/// Error when @p value would not fit an entity id table's buffer of @p max_length characters.
std::expected<void, std::string> check_id_length(const std::optional<std::string> &value, std::string_view key,
                                                 std::size_t max_length, const std::string &path, std::size_t index) {
  if (value && value->size() > max_length)
    return std::unexpected(
        std::format("Spawn points '{}' actors[{}] '{}' is longer than {} characters", path, index, key, max_length));
  return {};
}

/// The already type-checked raw fields of one spawn actor.
struct ActorFields {
  int col{};

  std::optional<std::string> dialogue_ref{};

  std::optional<std::string> facing{};

  std::optional<std::string> id{};

  int row{};

  std::string sprite_name{};
};

std::expected<ActorFields, std::string> read_actor_fields(const json &entry, const std::string &path,
                                                          std::size_t index) {
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

  return ActorFields{
      .col = *col,
      .dialogue_ref = *dialogue_ref,
      .facing = *facing,
      .id = *id,
      .row = *row,
      .sprite_name = *sprite_name,
  };
}

std::expected<void, std::string> validate_actor(const ActorFields &fields, const std::string &path, std::size_t index) {
  if (fields.col < 0 || fields.row < 0)
    return std::unexpected(std::format("Spawn points '{}' actors[{}] 'col' and 'row' must be >= 0", path, index));
  if (fields.sprite_name.empty())
    return std::unexpected(std::format("Spawn points '{}' actors[{}] 'sprite' must not be empty", path, index));
  if (auto checked = check_id_length(fields.id, "id", corundum::entities::ActorIdTable::k_max_id_len - 1, path, index);
      !checked)
    return std::unexpected(checked.error());
  if (auto checked = check_id_length(fields.dialogue_ref, "dialogue",
                                     corundum::entities::DialogueTable::k_max_id_len - 1, path, index);
      !checked)
    return std::unexpected(checked.error());
  if (fields.facing && !corundum::core::direction_from_name(*fields.facing))
    return std::unexpected(std::format("Spawn points '{}' actors[{}] 'facing' is not a valid direction", path, index));
  return {};
}

std::expected<Actor, std::string> parse_actor(const json &entry, const std::string &path, std::size_t index) {
  if (!entry.is_object())
    return std::unexpected(std::format("Spawn points '{}' actors[{}] must be an object", path, index));

  auto fields = read_actor_fields(entry, path, index);
  if (!fields)
    return std::unexpected(fields.error());
  if (auto checked = validate_actor(*fields, path, index); !checked)
    return std::unexpected(checked.error());

  Actor actor{
      .col = fields->col,
      .row = fields->row,
      .sprite_name = std::move(fields->sprite_name),
      .dialogue_ref = fields->dialogue_ref.value_or(""),
      .id = fields->id.value_or(""),
  };
  if (fields->facing)
    actor.facing = *fields->facing;
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

std::expected<SpawnPoints, std::string> load_spawn_points(const core::ContentSource &content,
                                                          const std::filesystem::path &path) {
  if (!content.contains(path))
    return SpawnPoints{};

  auto j_result = core::read_json(content, path, "spawn points");
  if (!j_result)
    return std::unexpected(std::move(j_result).error());
  json j = std::move(*j_result);

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
