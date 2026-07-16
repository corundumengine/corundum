#pragma once
#include <expected>
#include <optional>
#include <string>
#include <vector>

namespace corundum::gameplay::world {

  /**
   * @brief An NPC to spawn on a map at load time.
   *
   * col/row are tile coordinates; main.cpp converts to world-space pixels at spawn time.
   */
  struct Actor {
    int col = 0;
    int row = 0;
    std::string sprite_name;
    std::string dialogue_ref;     ///< Empty if the actor has no dialogue.
    std::string facing = "south"; ///< Direction the NPC faces; defaults to "south".
  };

  /** @brief Optional per-map player placement from a spawn-points file. */
  struct PlayerSpawn {
    float col = 0.f;
    float row = 0.f;
  };

  /** @brief Contents of a spawn-points JSON: NPCs plus optional player placement. */
  struct SpawnPoints {
    std::vector<Actor> actors;
    std::optional<PlayerSpawn> player;
  };

  /**
   * @brief Load spawn points from a JSON file.
   *
   * Expects an object with an "actors" array and an optional "player" object.
   * Returns a SpawnPoints with empty actors and no player if the file does not exist.
   *
   * @param path Path to the spawn points JSON (e.g. "data/spawn_points/world.json").
   * @return Loaded spawn points, or std::unexpected with an error description on failure.
   */
  [[nodiscard]] std::expected<SpawnPoints, std::string> load_spawn_points(const std::string &path);

  /**
   * @brief Load actors from a spawn points JSON file.
   *
   * Thin wrapper around load_spawn_points returning only the actors vector.
   * Expects an object with an "actors" array. Returns an empty vector if the
   * file does not exist.
   *
   * @note Prefer load_spawn_points when the caller also needs player placement.
   *
   * @param path Path to the spawn points JSON (e.g. "data/spawn_points/world.json").
   * @return Loaded actors, empty if file is absent, or std::unexpected with an
   *         error description on schema/parse failure.
   */
  [[nodiscard]] std::expected<std::vector<Actor>, std::string> load_actors(const std::string &path);

} // namespace corundum::gameplay::world
