// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <corundum/sprites/sprite.hpp>
#include <corundum/sprites/sprite_atlas.hpp>
#include <corundum/toolkit/editor/canvas_controller.hpp>
#include <corundum/toolkit/editor/undo.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace tools::spritesmith {

/** Selects between character animation sheet, flat sprite sheet, and read-only atlas.
 */
enum class SheetMode { Character, SpriteSheet, Atlas };

/** A named sprite entry within a character sheet.
 *
 *  Stores the grid footprint, anchor offset, and per-animation frame sequences
 *  matching the character sprite JSON schema.
 */
struct SpriteDefinition {
  /** Unique sprite name (key in "frames" object). */
  std::string name;

  /** Horizontal grid cell span (>= 1). */
  int col_span = 1;

  /** Vertical grid cell span (>= 1). */
  int row_span = 1;

  /** Collision footprint in tile-grid units, centred on the feet anchor by the runtime.
   *  Authored in tiles so the number means what the game uses.
   */
  float footprint_col_span = 0.25f;
  float footprint_row_span = 0.5f;

  /** Fractional Y offset defining the feet anchor. */
  float walk_around_offset = 0.6f;

  /** Playback rate override; 0 = use engine default. */
  float fps = 0.f;

  /** Per-animation frame sequences, indexed by AnimId. */
  std::array<std::vector<corundum::sprites::FrameCoord>, corundum::sprites::k_num_anim_ids> anim_frames;
};

/** A named animation clip within a sprite sheet sheet.
 */
struct AnimClip {
  /** Clip name. */
  std::string name;

  /** Ordered frame coordinates. */
  std::vector<corundum::sprites::FrameCoord> frames;
};

/** A named animation clip within an atlas, referencing frames by atlas sprite name.
 *
 *  Names (not rects/indices) survive spritepacker repacks; see atlas_clips.hpp.
 */
struct AtlasAnimClip {
  /** Clip name. */
  std::string name;

  /** Playback rate for this clip. */
  int fps = 8;

  /** Ordered atlas sprite names; may contain dangling names. */
  std::vector<std::string> frames;
};

/** Undo-stack snapshot for atlas mode: the authored clip data only.
 *
 *  Atlas geometry (atlas_sprites) is read-only and never mutated, so it is excluded.
 */
struct AtlasClipDoc {
  std::vector<AtlasAnimClip> clips;
  int selected_clip = -1;
};

/** All mutable state for a Spritesmith editing session.
 *
 *  Pure data — no GLFW, no I/O, no algorithms. Every subsystem receives a
 *  reference to this struct and reads/writes only the fields it owns.
 */
struct EditorState {
  // File

  /** Output JSON path (empty = unsaved new file). */
  std::filesystem::path json_path;

  /** True when unsaved changes exist. */
  bool dirty = false;

  corundum::toolkit::widgets::FileBrowserState open_browser;
  corundum::toolkit::widgets::FileBrowserState save_browser;
  bool show_open_confirm = false;
  std::filesystem::path pending_open_path;

  // Mode

  /** Active sheet type. */
  SheetMode mode = SheetMode::Character;

  // Sheet metadata (common)

  /** Logical identifier (used as "id" in character JSON). */
  std::string sheet_id;

  /** Path to the PNG sprite sheet image. */
  std::string image_path;

  /** Width of one frame cell in pixels. */
  int frame_width = 16;

  /** Height of one frame cell in pixels. */
  int frame_height = 16;

  /** Left-edge pixel offset to the first frame. */
  int offset_x = 0;

  /** Top-edge pixel offset to the first frame. */
  int offset_y = 0;

  /** Horizontal pixel gap between frame cells. */
  int spacing_x = 0;

  /** Vertical pixel gap between frame cells. */
  int spacing_y = 0;

  // Derived from loaded image (set by main after texture load)

  /** Pixel width of the loaded PNG. */
  int image_pixel_w = 0;

  /** Pixel height of the loaded PNG. */
  int image_pixel_h = 0;

  // Tile geometry (set by main from ToolConfig)
  /** Tile diamond size in pixels, used to draw the ground footprint at true scale in the
   *  animation preview. Should match the project's tilemap diamonds (sprite pixels and
   *  world pixels are the same unit).
   */
  int tile_diamond_w = 128;
  int tile_diamond_h = 64;

  // Character mode

  /** All sprite definitions in the sheet. */
  std::vector<SpriteDefinition> sprites;

  /** Index of the active sprite, or -1. */
  int selected_sprite = -1;

  /** Animation being viewed/recorded. */
  corundum::sprites::AnimId selected_anim = corundum::sprites::AnimId::South;

  /** True while recording frames into selected_anim. */
  bool anim_recording = false;

  // Sprite Sheet mode

  /** Number of tile columns (stored in sprite sheet JSON). */
  int columns = 0;

  /** Number of tile rows (stored in sprite sheet JSON). */
  int rows = 0;

  /** Playback FPS for sprite sheet animation clips. */
  int anim_fps = 2;

  /** All animation clips in the sprite sheet. */
  std::vector<AnimClip> anim_clips;

  /** Index of the active clip, or -1. */
  int selected_clip = -1;

  /** True while recording frames into selected_clip. */
  bool clip_recording = false;

  // Atlas mode (read-only geometry, sidecar-backed clips)

  /** Read-only, from the atlas JSON. */
  std::vector<corundum::sprites::AtlasSprite> atlas_sprites;

  /** Sprite name -> atlas_sprites index. */
  std::unordered_map<std::string, int> atlas_name_to_index;

  /** Authored clips (sidecar-backed). */
  std::vector<AtlasAnimClip> atlas_clips;

  /** Index of the active clip, or -1. */
  int selected_atlas_clip = -1;

  /** True while recording frames into selected_atlas_clip. */
  bool atlas_clip_recording = false;

  /** atlas_sprites index under the cursor, or -1. */
  int hover_sprite = -1;

  /** Focused entry in the sprite list panel, or -1. */
  int selected_atlas_sprite = -1;

  /** Undo/redo for atlas_clips edits. */
  corundum::toolkit::editor::UndoStack<AtlasClipDoc> atlas_undo;

  // Preview overlays

  /** Draw collision rect overlay in the animation preview. */
  bool show_collision_box = false;

  // Canvas view
  corundum::toolkit::editor::CanvasController canvas{0.25f, 16.f};

  // Hover state (updated on every mouse move)

  /** Frame column under the cursor, or -1. */
  int hover_col = -1;

  /** Frame row under the cursor, or -1. */
  int hover_row = -1;
};

} // namespace tools::spritesmith
