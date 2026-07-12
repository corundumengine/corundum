#pragma once
#include <corundum/resources/character_sheet_loader.hpp>
#include <nlohmann/json.hpp>

namespace corundum::resources {

  /** @brief Serialize a character sheet to JSON matching the engine's character sheet schema.
   *
   * @param[in] data  A fully-loaded CharacterSheetData.
   * @return JSON object suitable for write_json(); round-trips through load_character_sheet().
   */
  [[nodiscard]] nlohmann::json serialize_character_sheet(const CharacterSheetData &data);

} // namespace corundum::resources
