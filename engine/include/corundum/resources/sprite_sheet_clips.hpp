#pragma once
#include <corundum/resources/sprite.hpp>

#include <string>
#include <vector>

namespace corundum::resources {

  /** @brief One named animation clip — a sequence of frame coordinates. */
  struct AnimClip {
    std::string name;
    std::vector<FrameCoord> frames;
  };

  /** @brief Sprite-sheet clips format — a grid layout with optional animation clips.
   *
   * This is the format produced by spritesmith in SpriteSheet mode. It is
   * distinct from the character sheet format (CharacterSheetData) — this one
   * has a fixed grid (columns × rows) and optional named animation clips.
   */
  struct SpriteSheetClips {
    std::string id;
    std::string path;
    int columns = 0;
    int rows = 0;
    int frame_width = 0;
    int frame_height = 0;
    int offset_x = 0;
    int offset_y = 0;
    int spacing_x = 0;
    int spacing_y = 0;
    int anim_fps = 2;
    std::vector<AnimClip> clips;
  };

} // namespace corundum::resources
