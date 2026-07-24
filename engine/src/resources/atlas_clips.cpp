#include <corundum/resources/atlas_clips.hpp>

namespace corundum::resources {

  std::filesystem::path atlas_clips_sidecar_path(const std::filesystem::path &atlas_path) {
    std::filesystem::path p = atlas_path;
    return p.replace_extension(".spritedata.json");
  }

} // namespace corundum::resources
