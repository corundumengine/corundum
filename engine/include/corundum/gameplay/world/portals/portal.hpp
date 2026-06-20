#pragma once
#include <expected>
#include <string>
#include <vector>

namespace corundum::gameplay::world {

  /**
   * @brief A one-way map transition trigger in world-pixel space.
   *
   * Defines a rectangular region on the current map that, when overlapped by the
   * player, initiates a transition to @p target_map, spawning the player at the
   * tile coordinate (@p spawn_col, @p spawn_row) in that map.
   */
  struct Portal {
    float x = 0.f;          ///< Left edge of trigger rect in world pixels.
    float y = 0.f;          ///< Top edge of trigger rect in world pixels.
    float w = 0.f;          ///< Width of trigger rect in world pixels.
    float h = 0.f;          ///< Height of trigger rect in world pixels.
    std::string target_map; ///< Path to the target tilemap JSON.
    int spawn_col = 0;      ///< Tile column for player spawn in the target map.
    int spawn_row = 0;      ///< Tile row for player spawn in the target map.
  };

  /**
   * @brief Pending map transition request produced when the player steps on a portal.
   *
   * Written into GameState by the update system; consumed and cleared by the
   * platform shell (main.cpp) which performs the actual asset swap.
   */
  struct MapTransition {
    std::string target_map; ///< Path to the target tilemap JSON.
    int spawn_col = 0;      ///< Tile column for player spawn in the target map.
    int spawn_row = 0;      ///< Tile row for player spawn in the target map.
  };

  /**
   * @brief Load portals from a portal JSON file.
   *
   * Expects an object with a "portals" array. Each entry uses tile coordinates
   * (col, row, w, h) which are converted to world pixels via @p tile_w and
   * @p tile_h. Returns an empty vector if the file does not exist.
   *
   * @param path   Path to the portals JSON (e.g. "data/portals/world.json").
   * @param tile_w Tile width in world pixels (tile_width_px * tile_scale).
   * @param tile_h Tile height in world pixels (tile_height_px * tile_scale).
   * @return Loaded portals with world-pixel rects, empty if file is absent,
   *         or std::unexpected with an error description on schema/parse failure.
   */
  [[nodiscard]] std::expected<std::vector<Portal>, std::string> load_portals(const std::string &path, float tile_w,
                                                                             float tile_h);

} // namespace corundum::gameplay::world
