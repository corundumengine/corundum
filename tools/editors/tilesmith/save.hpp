#pragma once
#include "editor_state.hpp"
#include <expected>
#include <filesystem>
#include <string>

namespace tools::tilemap {

  /**
   * @brief Serialize state.map back to state.map_path.
   *
   * Reads the original JSON to preserve id/tilesets/placements verbatim.
   * Rebuilds the layers array using the sparse/dense encoding heuristic.
   * Also writes state.portals to data/portals/{stem}.json.
   * Sets state.dirty = false only after both files are written successfully.
   *
   * @param state Editor state to save.
   * @return An empty expected on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> save_tilemap(EditorState &state);

  /**
   * @brief Derive the portals file path from a tilemap path.
   *
   * e.g. "game/data/tilemaps/cave.json" → "data/portals/cave.json"
   *
   * @param map_path Path to the tilemap JSON file.
   * @return Path to the corresponding portals JSON file.
   */
  [[nodiscard]] std::filesystem::path portals_path(const std::filesystem::path &map_path);

  /**
   * @brief Load portals from data/portals/{stem}.json into state.portals.
   *
   * Silently succeeds with an empty vector if the file does not exist.
   * Throws std::runtime_error on a malformed JSON schema.
   *
   * @param state Editor state to populate.
   */
  void load_portals(EditorState &state);

} // namespace tools::tilemap
