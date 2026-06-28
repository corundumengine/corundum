#pragma once
#include <array>
#include <cmath>
#include <corundum/resources/sprite.hpp>
#include <cstdint>
#include <string>
#include <utility>

namespace corundum::gameplay::component {

  /// Axis-aligned collision footprint in tile-grid units.
  /// Populated once at spawn; eliminates per-frame registry lookups during collision.
  /// The collision AABB extends from (col - col_span/2, row - row_span) to (col + col_span/2, row)
  /// centered on the entity's feet position (col, row).
  struct BoundingBox {
    float col_span = 0.f; ///< Horizontal collision extent in tile columns.
    float row_span = 0.f; ///< Vertical collision extent in tile rows (upward from feet).
  };

  /// Sprite animation playback state.
  /// frame_counts is cached from the character registry at spawn so animate() never reads the
  /// registry in its hot loop.
  struct Animation {
    float timer = 0.f;
    float frame_duration = 0.15f;
    /// Frame count per AnimId; cached at spawn from the character registry.
    std::array<uint8_t, corundum::resources::k_num_anim_ids> frame_counts{};
  };

  /// Tile-grid position of an entity (feet position in fractional tile coordinates).
  struct Position {
    float col = 0.f;
    float row = 0.f;
  };

  /// Rendering component: identifies which sprite asset and animation frame to draw.
  struct Sprite {
    corundum::resources::SpriteId sprite_id = corundum::resources::k_null_sprite_id; ///< Interned sprite asset.
    corundum::resources::AnimId anim_id = corundum::resources::AnimId::Default;      ///< Current animation.
    uint8_t frame_index = 0;                                                         ///< Frame within anim_id.
  };

  /// Marks an entity as the trigger for a dialogue graph.
  /// graph_id must match the "id" field in the JSON file and the key in the graph registry.
  struct DialogueRef {
    std::string graph_id;
  };

  /// Per-frame movement delta in tile-grid units.
  struct Velocity {
    float dc = 0.f;
    float dr = 0.f;
  };

  /// Cardinal and intercardinal directions.
  enum class FacingDir : uint8_t { South, North, East, West, NorthEast, SouthEast, SouthWest, NorthWest };

  /// Lookup table mapping FacingDir to its opposite direction.
  inline constexpr std::array<FacingDir, 8> k_opposite_dir = {
      FacingDir::North,     // South
      FacingDir::South,     // North
      FacingDir::West,      // East
      FacingDir::East,      // West
      FacingDir::SouthWest, // NorthEast
      FacingDir::NorthWest, // SouthEast
      FacingDir::NorthEast, // SouthWest
      FacingDir::SouthEast, // NorthWest
  };

  /// Last-faced direction; used by animate() to pick directional idle animations.
  /// Only entities with directional idle behaviour (e.g. the player) receive this component.
  struct Facing {
    FacingDir dir = FacingDir::South;
  };

  /// Return the direction directly opposite to dir.
  [[nodiscard]] inline constexpr FacingDir opposite(FacingDir dir) noexcept {
    return k_opposite_dir[std::to_underlying(dir)];
  }

  /// Euclidean distance between two tile-grid positions.
  [[nodiscard]] inline float distance(Position a, Position b) noexcept {
    const float dcol = a.col - b.col;
    const float drow = a.row - b.row;
    return std::sqrt(dcol * dcol + drow * drow);
  }

} // namespace corundum::gameplay::component
