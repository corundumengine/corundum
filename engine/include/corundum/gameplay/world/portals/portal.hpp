#pragma once
#include <expected>
#include <string>
#include <vector>

namespace corundum::gameplay::world {

  /**
   * @brief A one-way map transition trigger in tile-grid space.
   *
   * Defines a rectangular region on the current map that, when overlapped by the
   * player, initiates a transition to @p target_map, spawning the player at the
   * tile coordinate (@p spawn_col, @p spawn_row) in that map.
   *
   * Stored in the same tile-grid units as collision rects (see
   * corundum::gameplay::world::tilemap::CollisionRect) rather than world pixels, so
   * the physics system can test the player's AABB against it directly — no
   * per-frame cart_to_iso/iso_to_cart round trip needed.
   */
  struct Portal {
    float col = 0.f;        ///< Left tile column of the trigger rect.
    float row = 0.f;        ///< Top tile row of the trigger rect.
    float w = 0.f;          ///< Width of trigger rect in tiles.
    float h = 0.f;          ///< Height of trigger rect in tiles.
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
   * Expects an object with a "portals" array, each entry giving a tile-grid rect
   * (col, row, w, h) — the same coordinate space tilesmith authors and saves them
   * in, so no conversion happens at load time. Returns an empty vector if the
   * file does not exist.
   *
   * @param path Path to the portals JSON (e.g. "data/portals/world.json").
   * @return Loaded portals in tile-grid space, empty if file is absent, or
   *         std::unexpected with an error description on schema/parse failure.
   */
  [[nodiscard]] std::expected<std::vector<Portal>, std::string> load_portals(const std::string &path);

} // namespace corundum::gameplay::world
