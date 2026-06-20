#pragma once
#include <expected>
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

  /**
   * @brief Load actors from a spawn points JSON file.
   *
   * Expects an object with an "actors" array. Returns an empty vector if the
   * file does not exist.
   *
   * @param path Path to the spawn points JSON (e.g. "data/spawn_points/world.json").
   * @return Loaded actors, empty if file is absent, or std::unexpected with an
   *         error description on schema/parse failure.
   */
  [[nodiscard]] std::expected<std::vector<Actor>, std::string> load_actors(const std::string &path);

} // namespace corundum::gameplay::world
