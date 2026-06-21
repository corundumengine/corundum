#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace tools::tilemap {

  /**
   * @brief Transient state for the New Tilemap creation dialog.
   *
   * Owned by main() in new-map mode. Fields are written by ImGui widgets
   * each frame and read when the user clicks "Create".
   */
  struct NewMapDialogState {
    char id[128] = {};
    int width = 20;
    int height = 15;
    int iso_diamond_w = 128; ///< Tile footprint width in pixels.
    int iso_diamond_h = 64;  ///< Tile footprint height in pixels.
    char layer_name[64] = "ground";
    char tileset_source[512] = {};
    std::string error_msg;
    bool confirmed = false;
    bool cancelled = false;
  };

  /**
   * @brief Render the "New Tilemap" modal popup.
   *
   * Must be called every frame while the dialog is active. Sets
   * dlg.confirmed or dlg.cancelled when the user acts.
   *
   * @param [in,out] dlg Dialog state updated each frame.
   */
  void render_new_map_dialog(NewMapDialogState &dlg);

  /**
   * @brief Write a minimal tilemap JSON to data/tilemaps/<dlg.id>.json.
   *
   * @param [in] dlg Validated dialog state.
   * @return The path written on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<std::filesystem::path, std::string> write_new_tilemap_json(const NewMapDialogState &dlg);

} // namespace tools::tilemap
