#pragma once
#include <concepts>
#include <corundum/gameplay/entity/entity.hpp>
#include <ranges>
#include <type_traits>

namespace corundum::gameplay::component {

  // Tables use EntityId and k_max_entities ubiquitously.
  using corundum::gameplay::entity::EntityId;
  using corundum::gameplay::entity::k_max_entities;

  /** @brief A valid game table is trivially copyable, exposes an integer count,
   *         and provides a contiguous active-element span.
   *
   * Tables store component data in SoA layout. The active_span() method returns
   * a contiguous range over the live (dense) elements — callers iterate this
   * span rather than the full fixed-size array.
   */
  template <typename T>
  concept GameTable = requires(T &t, const T &ct) {
    { t.count } -> std::convertible_to<int>;
    { t.active_span() } -> std::ranges::contiguous_range;
    requires std::is_trivially_copyable_v<T>;
  };

} // namespace corundum::gameplay::component
