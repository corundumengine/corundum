#pragma once
#include <corundum/resources/atlas_clips.hpp>
#include <nlohmann/json.hpp>

namespace corundum::resources {

  /** @brief Serialize atlas clips data to JSON.
   *
   * Writes schema_version: 1 on save.
   *
   * @param[in] data  The atlas clips data.
   * @return JSON object suitable for write_json(); round-trips through load_atlas_clips().
   */
  [[nodiscard]] nlohmann::json serialize_atlas_clips(const AtlasClipsData &data);

} // namespace corundum::resources
