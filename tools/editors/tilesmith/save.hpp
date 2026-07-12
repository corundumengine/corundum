#pragma once
#include "editor_state.hpp"
#include <expected>
#include <filesystem>
#include <string>

namespace tools::tilemap {

  /** @brief Serialize state.map to state.map_path using engine serializers.
   *
   * Reads the original JSON for base-merge (preserves unknown keys), then
   * calls serialize_tilemap() + serialize_tiledata() for tilesets and
   * serialize_portals() for the portal file. All engine-managed fields are
   * overwritten; unknown keys in the source JSON survive unchanged.
   *
   * Sets state.dirty = false on success.
   *
   * @param state Editor state to save.
   * @return An empty expected on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> save_tilemap(EditorState &state);

  /** @brief Derive the portals file path from a tilemap path.
   *
   * e.g. "data/tilemaps/cave.json" → "data/portals/cave.json"
   *
   * @param map_path Path to the tilemap JSON file.
   * @return Path to the corresponding portals JSON file.
   */
  [[nodiscard]] std::filesystem::path portals_path(const std::filesystem::path &map_path);

  /** @brief Load portals via the engine loader into state.portals.
   *
   * Silently succeeds with an empty vector if the file does not exist.
   *
   * @param state Editor state to populate.
   * @return An empty expected on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> load_portals(EditorState &state);

} // namespace tools::tilemap
