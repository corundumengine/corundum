// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/fixed_id_column.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <string_view>

namespace corundum::entities {

  /** @brief SoA table for the stable authoring id of an actor.
   *
   * One row per spawned entity that carries a non-empty `Actor::id`. The id is
   * stored as a fixed-size `char[]` buffer so the table remains trivially
   * copyable (required by the GameTable concept). Quests and saves reference
   * NPCs by this id — it survives chunk respawns that change the entity handle.
   */
  struct ActorIdTable : FixedIdColumn<k_max_entities> {
    static constexpr auto k_max = k_max_entities;

    /** @brief Actor id string for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return Non-owning string_view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get_actor_id(EntityId e) const noexcept {
      return id_of(e);
    }
  };

  static_assert(GameTable<ActorIdTable>);

} // namespace corundum::entities
