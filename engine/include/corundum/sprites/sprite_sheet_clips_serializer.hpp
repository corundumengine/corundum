#pragma once
#include <corundum/sprites/sprite_sheet_clips.hpp>
#include <nlohmann/json.hpp>

namespace corundum::sprites {

  /** @brief Serialize a sprite-sheet clips struct to JSON.
   *
   * Writes schema_version: 1 on save.
   *
   * @param[in] data  The sprite-sheet clips data.
   * @return JSON object suitable for write_json(); round-trips through load_sprite_sheet_clips().
   */
  [[nodiscard]] nlohmann::json serialize_sprite_sheet_clips(const SpriteSheetClips &data);

} // namespace corundum::sprites
