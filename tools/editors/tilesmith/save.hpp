#pragma once
#include "editor_state.hpp"
#include <expected>
#include <filesystem>
#include <string>

namespace tools::tilesmith {

  /** @brief Serialize state.map to state.map_path using engine serializers.
   *
   * Reads the original JSON for base-merge (preserves unknown keys), then
   * calls serialize_tilemap() and serialize_portals() for the portal file.
   * All engine-managed fields are overwritten; unknown keys in the source
   * JSON survive unchanged.
   *
   * Sets state.dirty = false on success.
   *
   * @param state Editor state to save.
   * @return An empty expected on success, or an error message string on failure.
   */
  [[nodiscard]] std::expected<void, std::string> save_tilemap(EditorState &state);

  /** @brief Validate state.map, then save via save_tilemap() if valid.
   *
   * On validation failure, sets state.show_validation_popup/state.validation_errors instead of
   * saving (the popup itself is rendered by handle_input() — see input.cpp). On a genuine I/O
   * failure, sets state.last_io_error/state.show_io_error_popup. This is the single save entry
   * point — both Ctrl+S (input.cpp) and File > Save/Save As/Save & Exit (menu.cpp) call this
   * instead of each re-implementing "validate, then save".
   *
   * @param state Editor state to save.
   */
  void try_save(EditorState &state);

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

} // namespace tools::tilesmith
