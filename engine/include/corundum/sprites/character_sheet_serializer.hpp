#pragma once
#include <corundum/sprites/character_sheet_loader.hpp>
#include <nlohmann/json.hpp>

namespace corundum::sprites {

  /** @brief Serialize a character sheet to JSON matching the engine's character sheet schema.
   *
   * @param[in] data  A fully-loaded CharacterSheetData.
   * @return JSON object suitable for write_json(); round-trips through load_character_sheet().
   */
  [[nodiscard]] nlohmann::json serialize_character_sheet(const CharacterSheetData &data);

} // namespace corundum::sprites
