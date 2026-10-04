// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/direction.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/sprites/sprite.hpp>

#include <optional>

namespace corundum::gameplay::dialogue {

  /** @brief The NPC bound to the active dialogue, with the facing and animation saved when the
   *  conversation began so they can be restored when it ends.
   *
   *  The saved values are set only when the NPC had the corresponding component at bind time.
   */
  struct DialogueNpc {
    corundum::entities::EntityId entity{};

    std::optional<corundum::sprites::AnimId> saved_anim{};

    std::optional<corundum::core::Direction> saved_facing{};
  };

} // namespace corundum::gameplay::dialogue
