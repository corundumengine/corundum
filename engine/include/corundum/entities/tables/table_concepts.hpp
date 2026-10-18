// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <concepts>
#include <corundum/entities/entity.hpp>
#include <ranges>
#include <type_traits>

namespace corundum::entities {

  /** @brief A valid game table is trivially copyable, exposes an integer count,
   *         a generation-aware has()/remove() pair, and a contiguous
   *         active-element span.
   *
   * Tables store component data in SoA layout. The active_span() method returns
   * a contiguous range over the live (dense) elements — callers iterate this
   * span rather than the full fixed-size array. has()/remove() are the generic
   * despawn contract: World::despawn() calls them on every table in all_tables().
   */
  template <typename T>
  concept GameTable = requires(T &t, const T &ct, EntityId e) {
    { t.count } -> std::convertible_to<int>;
    { ct.has(e) } -> std::same_as<bool>;
    { t.remove(e) } -> std::same_as<void>;
    { t.active_span() } -> std::ranges::contiguous_range;
    requires std::is_trivially_copyable_v<T>;
  };

} // namespace corundum::entities
