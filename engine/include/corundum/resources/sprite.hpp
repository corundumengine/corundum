#pragma once
#include <array>
#include <cstdint>
#include <flat_map>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::resources {

  using Id = uint16_t;
  inline constexpr Id k_null_sheet = 0;

  using SpriteId = uint16_t;
  inline constexpr SpriteId k_null_sprite_id = 0;

  /// Animation identifiers matching JSON sprite sheet keys; interned at load time.
  /// Each value names a facing direction; the sprite sheet itself determines the motion type
  /// (walk, idle, attack, etc.) — switching sheets switches the motion, not the AnimId.
  enum class AnimId : uint8_t {
    Default = 0,
    North,
    NorthEast,
    East,
    SouthEast,
    South,
    SouthWest,
    West,
    NorthWest,
    COUNT,
  };

  /// Total number of named animations; use as array extent.
  inline constexpr uint8_t k_num_anim_ids = static_cast<uint8_t>(AnimId::COUNT);

  /// Canonical string names for each AnimId, indexed by AnimId value.
  inline constexpr std::array<std::string_view, k_num_anim_ids> k_anim_names = {
      "default", "north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest",
  };

  /// Resolves an animation name to its AnimId; returns AnimId::COUNT if unknown.
  [[nodiscard]] inline constexpr AnimId anim_name_to_id(std::string_view name) noexcept {
    if (name == "default")
      return AnimId::Default;
    if (name == "north")
      return AnimId::North;
    if (name == "northeast")
      return AnimId::NorthEast;
    if (name == "east")
      return AnimId::East;
    if (name == "southeast")
      return AnimId::SouthEast;
    if (name == "south")
      return AnimId::South;
    if (name == "southwest")
      return AnimId::SouthWest;
    if (name == "west")
      return AnimId::West;
    if (name == "northwest")
      return AnimId::NorthWest;
    return AnimId::COUNT;
  }

  /// Texture atlas packing metadata; geometry fields are read every rendered frame.
  struct SpriteSheet {
    Id id = k_null_sheet; ///< Unique identifier
    std::string path;     ///< File path to the sprite sheet JSON (cold; for debugging/reload only)
    int frame_width = 0;  ///< Width of one grid cell in pixels
    int frame_height = 0; ///< Height of one grid cell in pixels
    int offset_x = 0;     ///< Pixel offset from left edge of image to the first frame
    int offset_y = 0;     ///< Pixel offset from top edge of image to the first frame
    int spacing_x = 0;    ///< Horizontal gap in pixels between frames
    int spacing_y = 0;    ///< Vertical gap in pixels between frames
    int columns = 0;      ///< Number of frames horizontally in the sheet
    int rows = 0;         ///< Number of frames vertically in the sheet
  };

  /// Grid location of a single animation frame within a sprite sheet.
  struct FrameCoord {
    int col = 0; ///< Column index
    int row = 0; ///< Row index
  };

  /// Total rendered pixel width for a sprite occupying col_span grid columns.
  /// @pre col_span >= 1
  [[nodiscard]] inline constexpr int rendered_frame_width(int col_span, int frame_width, int spacing_x) noexcept {
    return col_span * frame_width + (col_span - 1) * spacing_x;
  }

  /// Total rendered pixel height for a sprite occupying row_span grid rows.
  /// @pre row_span >= 1
  [[nodiscard]] inline constexpr int rendered_frame_height(int row_span, int frame_height, int spacing_y) noexcept {
    return row_span * frame_height + (row_span - 1) * spacing_y;
  }

  /** @brief Integer pixel-space point. */
  struct IntPoint {
    int x = 0;
    int y = 0;
  };

  /** @brief Compute the pixel-space top-left origin of a frame cell within a sprite-sheet grid.
   *  @param offset_x  Pixel offset from left edge of image to the first frame.
   *  @param offset_y  Pixel offset from top edge of image to the first frame.
   *  @param frame_w   Width of one grid cell in pixels.
   *  @param frame_h   Height of one grid cell in pixels.
   *  @param spacing_x Horizontal gap between frame cells in pixels.
   *  @param spacing_y Vertical gap between frame cells in pixels.
   *  @param col       Frame column index.
   *  @param row       Frame row index.
   *  @return The {x, y} origin of the frame cell in image pixel space. */
  [[nodiscard]] inline constexpr IntPoint frame_origin(int offset_x, int offset_y, int frame_w, int frame_h,
                                                       int spacing_x, int spacing_y, int col, int row) noexcept {
    return {offset_x + col * (frame_w + spacing_x), offset_y + row * (frame_h + spacing_y)};
  }

  /** @brief Compute the pixel-space top-left origin of a frame within the sheet's grid.
   *  @param sheet The sprite sheet providing grid geometry (offset, frame size, spacing).
   *  @param fc    Frame coordinate (column, row) within the sheet.
   *  @return The {x, y} origin of the frame in image pixel space.
   *  @note Delegates to the raw-parameter overload. */
  [[nodiscard]] inline constexpr IntPoint frame_origin(const SpriteSheet &sheet, FrameCoord fc) noexcept {
    return frame_origin(sheet.offset_x, sheet.offset_y, sheet.frame_width, sheet.frame_height, sheet.spacing_x,
                        sheet.spacing_y, fc.col, fc.row);
  }

  /// Per-sprite frame data for a character; hot-path access goes through anim_frames.
  /// col_span/row_span express the character's footprint in grid-cell units,
  /// e.g. col_span=1, row_span=2 for a 1x2-tile-tall character.
  struct Frames {
    Id sheet_id = k_null_sheet;            ///< Owning sprite sheet
    int col_span = 1;                      ///< Grid columns the sprite occupies
    int row_span = 1;                      ///< Grid rows the sprite occupies
    int collision_w = 0;                   ///< Collision width in sprite pixels; 0 = use rendered frame width
    int collision_h = 0;                   ///< Collision height in sprite pixels; 0 = use rendered frame height
    float walk_around_offset = 0.f;        ///< Fraction of sprite height locating the feet
    float fps = 0.f;                       ///< Playback rate override; 0 = use engine default
    SpriteId sprite_id = k_null_sprite_id; ///< Interned identifier for O(1) lookup
    std::flat_map<std::string, std::vector<FrameCoord>> animations;  ///< Frames by name; populated at load time only
    std::array<std::vector<FrameCoord>, k_num_anim_ids> anim_frames; ///< Hot-path frame layout indexed by AnimId
  };

} // namespace corundum::resources
