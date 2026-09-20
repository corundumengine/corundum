// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/sprites/character_sheet_loader.hpp>
#include <nlohmann/json.hpp>

namespace corundum::sprites {

  /** @brief Serialize a character sheet to JSON matching the engine's character sheet schema.
   *
   * Omitted keys are ones the loader restores from a default: `offset_*`, `spacing_*`, and `fps`
   * when zero, `walk_around_offset` when equal to k_default_walk_around_offset, and the footprint
   * spans when the entry was not authored with them (`CharacterSpriteEntry::footprint_authored`).
   * Reloading the output therefore reconstructs @p data exactly, including `footprint_authored`.
   *
   * @param[in] data  A fully-loaded CharacterSheetData.
   * @pre Every sprite has a non-empty, unique `name`; a duplicate would overwrite the earlier
   *      entry in the "frames" object.
   * @return JSON object suitable for write_json(); round-trips through load_character_sheet().
   */
  [[nodiscard]] nlohmann::json serialize_character_sheet(const CharacterSheetData &data);

} // namespace corundum::sprites
