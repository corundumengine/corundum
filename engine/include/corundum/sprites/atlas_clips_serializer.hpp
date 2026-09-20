// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/sprites/atlas_clips.hpp>
#include <nlohmann/json.hpp>

namespace corundum::sprites {

  /** @brief Serialize atlas clips data to JSON.
   *
   * Writes schema_version: 1 on save and omits "fps" when it equals
   * k_default_clip_fps (load_atlas_clips() restores the default).
   *
   * @param[in] data  The atlas clips data.
   * @pre Every clip has a non-empty, unique `name`, a non-empty `frames` list of
   *      non-empty names, and a positive `fps`; otherwise the output will not
   *      round-trip through load_atlas_clips().
   * @return JSON object suitable for write_json(); round-trips through load_atlas_clips().
   */
  [[nodiscard]] nlohmann::json serialize_atlas_clips(const AtlasClipsData &data);

} // namespace corundum::sprites
