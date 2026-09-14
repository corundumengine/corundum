// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <array>
#include <cmath>
#include <corundum/core/direction.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <string>

namespace corundum::entities {

  /// Animation input for a spawned entity: the per-AnimId frame counts read from the character registry.
  /// Spawn input only — the playback timer and frame duration live in the AnimationTable, which caches
  /// these counts so animation::update never reads the registry in its hot loop.
  struct Animation {
    /// Frame count per AnimId; 0 means the clip is absent.
    std::array<uint8_t, corundum::sprites::k_num_anim_ids> frame_counts{};
  };

  /// Tile-grid position of an entity (feet position in fractional tile coordinates).
  struct Position {
    float col = 0.f;
    float row = 0.f;
  };

  /// Rendering component: identifies which sprite asset and animation frame to draw.
  struct Sprite {
    corundum::sprites::SpriteId sprite_id = corundum::sprites::k_null_sprite_id; ///< Interned sprite asset.
    corundum::sprites::AnimId anim_id = corundum::sprites::AnimId::Default;      ///< Current animation.
    uint8_t frame_index = 0;                                                     ///< Frame within anim_id.
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

  /// Last-faced direction; used by animation::update to pick directional idle animations.
  /// Only entities with directional idle behaviour (e.g. the player) receive this component.
  struct Facing {
    corundum::core::Direction dir = corundum::core::Direction::South;
  };

  /// Euclidean distance between two tile-grid positions.
  [[nodiscard]] inline float distance(Position a, Position b) noexcept {
    const float dcol = a.col - b.col;
    const float drow = a.row - b.row;
    return std::sqrt((dcol * dcol) + (drow * drow));
  }

} // namespace corundum::entities
