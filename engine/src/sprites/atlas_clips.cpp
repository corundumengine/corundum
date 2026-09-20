// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/sprites/atlas_clips.hpp>
#include <filesystem>

namespace corundum::sprites {

  std::filesystem::path atlas_clips_sidecar_path(std::filesystem::path atlas_path) {
    atlas_path.replace_extension(".spritedata.json");
    return atlas_path;
  }

} // namespace corundum::sprites
