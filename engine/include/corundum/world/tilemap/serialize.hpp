// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/tilemap/tilemap.hpp>
#include <nlohmann/json_fwd.hpp>

namespace corundum::world::tilemap {

/** Serialize a Tilemap to JSON.
 *
 *  When `base` is non-null, the result starts as a copy of `base` and only
 *  the engine-managed fields are overwritten. Unknown keys present in `base`
 *  are preserved in the output. When `base` is null, a fresh JSON object is
 *  produced containing only the engine-managed fields.
 *
 *  @param map   The in-memory tilemap to serialize.
 *  @param base  Optional existing JSON to merge onto (preserves unknown keys).
 *  @return JSON object suitable for write_json().
 */
[[nodiscard]] nlohmann::json serialize(const Tilemap &map, const nlohmann::json *base = nullptr);

} // namespace corundum::world::tilemap
