#pragma once
#include <string>

namespace tools::tilemap {

  /**
   * @brief Editable portal definition using tile-grid coordinates.
   *
   * Mirrors the data/portals/{stem}.json schema. Unlike core::game::Portal,
   * positions are in tile coordinates rather than world pixels.
   */
  struct PortalEntry {
    int col = 0;            ///< Left tile column of the trigger rect.
    int row = 0;            ///< Top tile row of the trigger rect.
    int w = 1;              ///< Width in tiles.
    int h = 1;              ///< Height in tiles.
    std::string target_map; ///< Path to the target tilemap JSON.
    int spawn_col = 0;      ///< Spawn column in the target map.
    int spawn_row = 0;      ///< Spawn row in the target map.
  };

} // namespace tools::tilemap
