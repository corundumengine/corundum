// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/fixed_id_column.hpp>
#include <corundum/entities/tables/table_concepts.hpp>
#include <string_view>

namespace corundum::entities {

  /** @brief SoA table for the DialogueRef component.
   *
   * Only NPCs that can initiate dialogue appear here. The graph id is stored as
   * a fixed-size `char[]` buffer so the table remains trivially copyable
   * (required by the GameTable concept).
   */
  struct DialogueTable : FixedIdColumn<k_max_entities> {
    static constexpr auto k_max = k_max_entities;

    /** @brief Set (or replace) the graph ID for @p e.
     *  @param[in] e  Entity to update. @pre has(e) must be true.
     *  @param[in] id New graph identifier (truncated to k_max_id_len-1 if longer).
     */
    void set_graph_id(EntityId e, std::string_view id) noexcept {
      assign(e, id);
    }

    /** @brief Graph ID string for @p e as a non-owning view.
     *  @param[in] e Entity to query. @pre has(e).
     *  @return Non-owning string_view valid until the next insert/remove.
     */
    [[nodiscard]] std::string_view get_graph_id(EntityId e) const noexcept {
      return id_of(e);
    }
  };

  static_assert(GameTable<DialogueTable>);

} // namespace corundum::entities
