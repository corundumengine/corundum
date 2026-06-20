#pragma once
#include <array>
#include <cmath>
#include <corundum/resources/sprite.hpp>
#include <cstdint>
#include <string>
#include <utility>

namespace corundum::gameplay::ecs {

  /// Axis-aligned collision footprint derived from sprite registry data.
  /// Populated once at spawn; eliminates per-frame registry lookups during collision.
  struct BoundingBox {
    float w = 0.f;  ///< Collision width in world units.
    float h = 0.f;  ///< Sprite height in world units.
    float yo = 0.f; ///< Y offset from position.y to the top of the collision rect.
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

  /// World-space position of an entity.
  struct Position {
    float x = 0.f;
    float y = 0.f;
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

  /// Per-frame movement delta in world units.
  struct Velocity {
    float dx = 0.f;
    float dy = 0.f;
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

  /// Euclidean distance between two positions, in world units.
  [[nodiscard]] inline float distance(Position a, Position b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
  }

} // namespace corundum::gameplay::ecs
