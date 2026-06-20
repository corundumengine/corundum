#pragma once
#include <array>
#include <corundum/resources/sprite.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace tools::sprite {

  /**
   * @brief Selects between character animation sheet and flat sprite sheet.
   */
  enum class SheetMode { Character, SpriteSheet };

  /**
   * @brief A named sprite entry within a character sheet.
   *
   * Stores the grid footprint, anchor offset, and per-animation frame sequences
   * matching the character sprite JSON schema.
   */
  struct SpriteDefinition {
    std::string name;                ///< Unique sprite name (key in "frames" object).
    int col_span = 1;                ///< Horizontal grid cell span (>= 1).
    int row_span = 1;                ///< Vertical grid cell span (>= 1).
    int collision_w = 0;             ///< Collision width in sprite pixels; 0 = full frame width.
    int collision_h = 0;             ///< Collision height in sprite pixels; 0 = full frame height.
    float walk_around_offset = 0.6f; ///< Fractional Y offset defining the feet anchor.
    /// Per-animation frame sequences, indexed by AnimId.
    std::array<std::vector<corundum::resources::FrameCoord>, corundum::resources::k_num_anim_ids> anim_frames;
  };

  /**
   * @brief A named animation clip within a sprite sheet sheet.
   */
  struct AnimClip {
    std::string name;                                    ///< Clip name.
    std::vector<corundum::resources::FrameCoord> frames; ///< Ordered frame coordinates.
  };

  /**
   * @brief All mutable state for a Spritesmith editing session.
   *
   * Pure data — no GLFW, no I/O, no algorithms. Every subsystem receives a
   * reference to this struct and reads/writes only the fields it owns.
   */
  struct EditorState {
    // ---- File ----------------------------------------------------------------
    std::filesystem::path json_path; ///< Output JSON path (empty = unsaved new file).
    bool dirty = false;              ///< True when unsaved changes exist.

    // ---- Mode ----------------------------------------------------------------
    SheetMode mode = SheetMode::Character; ///< Active sheet type.

    // ---- Sheet metadata (common) --------------------------------------------
    std::string sheet_id;   ///< Logical identifier (used as "id" in character JSON).
    std::string image_path; ///< Path to the PNG sprite sheet image.
    int frame_width = 16;   ///< Width of one frame cell in pixels.
    int frame_height = 16;  ///< Height of one frame cell in pixels.
    int offset_x = 0;       ///< Left-edge pixel offset to the first frame.
    int offset_y = 0;       ///< Top-edge pixel offset to the first frame.
    int spacing_x = 0;      ///< Horizontal pixel gap between frame cells.
    int spacing_y = 0;      ///< Vertical pixel gap between frame cells.

    // ---- Derived from loaded image (set by main after texture load) ---------
    int image_pixel_w = 0; ///< Pixel width of the loaded PNG.
    int image_pixel_h = 0; ///< Pixel height of the loaded PNG.

    // ---- Character mode -----------------------------------------------------
    std::vector<SpriteDefinition> sprites; ///< All sprite definitions in the sheet.
    int selected_sprite = -1;              ///< Index of the active sprite, or -1.
    corundum::resources::AnimId selected_anim =
        corundum::resources::AnimId::South; ///< Animation being viewed/recorded.
    bool anim_recording = false;            ///< True while recording frames into selected_anim.

    // ---- Sprite Sheet mode -------------------------------------------------------
    int columns = 0;                  ///< Number of tile columns (stored in sprite sheet JSON).
    int rows = 0;                     ///< Number of tile rows (stored in sprite sheet JSON).
    int anim_fps = 2;                 ///< Playback FPS for sprite sheet animation clips.
    std::vector<AnimClip> anim_clips; ///< All animation clips in the sprite sheet.
    int selected_clip = -1;           ///< Index of the active clip, or -1.
    bool clip_recording = false;      ///< True while recording frames into selected_clip.

    // ---- Preview overlays ---------------------------------------------------
    bool show_collision_box = false; ///< Draw collision rect overlay in the animation preview.

    // ---- Canvas view --------------------------------------------------------
    float zoom = 2.0f;     ///< Display scale factor (1 = native pixel size).
    float camera_x = 0.0f; ///< Horizontal scroll offset in screen pixels.
    float camera_y = 0.0f; ///< Vertical scroll offset in screen pixels.

    // ---- Hover state (updated on every mouse move) --------------------------
    int hover_col = -1; ///< Frame column under the cursor, or -1.
    int hover_row = -1; ///< Frame row under the cursor, or -1.

    // ---- Middle-mouse pan state ---------------------------------------------
    bool panning = false;        ///< True while a middle-mouse pan is active.
    int pan_anchor_x = 0;        ///< Window-space X at pan start.
    int pan_anchor_y = 0;        ///< Window-space Y at pan start.
    float pan_start_cam_x = 0.f; ///< camera_x at pan start.
    float pan_start_cam_y = 0.f; ///< camera_y at pan start.
  };

} // namespace tools::sprite
