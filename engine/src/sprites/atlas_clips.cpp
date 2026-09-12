// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/atlas_clips.hpp>

namespace corundum::sprites {

  std::filesystem::path atlas_clips_sidecar_path(const std::filesystem::path &atlas_path) {
    std::filesystem::path p = atlas_path;
    return p.replace_extension(".spritedata.json");
  }

} // namespace corundum::sprites
