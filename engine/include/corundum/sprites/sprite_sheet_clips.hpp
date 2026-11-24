// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/sprites/sprite.hpp>

#include <string>
#include <vector>

namespace corundum::sprites {

/** Sprite-sheet clips JSON schema version this engine understands.
 *
 *  Written by serialize_sprite_sheet_clips() and gated by load_sprite_sheet_clips(). A file
 *  that omits the field is read as version 1. Pre-1.0 the format changes in place, so the
 *  version stays 1.
 */
inline constexpr int k_sprite_sheet_clips_schema_version = 1;

/** Playback rate applied when a sheet omits its "animations.fps". */
inline constexpr int k_default_anim_fps = 2;

/** One named animation clip — a sequence of frame coordinates. */
struct AnimClip {
  /** Clip identifier, unique within the sheet; must not be empty. */
  std::string name;

  /** Frames in playback order; may be empty while a clip is being authored. */
  std::vector<FrameCoord> frames;
};

/** Sprite-sheet clips format — a grid layout with optional animation clips.
 *
 *  This is the format produced by spritesmith in SpriteSheet mode. It is
 *  distinct from the character sheet format (CharacterSheetData) — this one
 *  has a fixed grid (columns × rows) and optional named animation clips.
 *
 *  A frame's (col, row) identifies a grid cell; its pixel origin is computed by frame_origin()
 *  from the frame size, offset, and spacing. Fields are grouped by topic, not alphabetized.
 */
struct SpriteSheetClips {
  /** Sheet identifier; may be empty. */
  std::string id;

  /** Image path verbatim from the JSON — not resolved against the sheet file's directory. */
  std::string path;

  /** Grid width in frame cells. */
  int columns = 0;

  /** Grid height in frame cells. */
  int rows = 0;

  /** Width of one grid cell in pixels. */
  int frame_width = 0;

  /** Height of one grid cell in pixels. */
  int frame_height = 0;

  /** Pixel offset from the image's left edge to the first frame. */
  int offset_x = 0;

  /** Pixel offset from the image's top edge to the first frame. */
  int offset_y = 0;

  /** Horizontal gap in pixels between frame cells. */
  int spacing_x = 0;

  /** Vertical gap in pixels between frame cells. */
  int spacing_y = 0;

  /** Sheet-wide default playback rate, in frames per second. */
  int anim_fps = k_default_anim_fps;

  /** Named clips; empty when the sheet is a plain grid. */
  std::vector<AnimClip> clips;
};

} // namespace corundum::sprites
