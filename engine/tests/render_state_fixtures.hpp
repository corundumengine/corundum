// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/render/render_state.hpp>

#include <cassert>
#include <variant>

namespace corundum::test {

  /// @return The WorldArea alternative of @p state.
  /// @pre @p state is in World mode.
  [[nodiscard]] inline render::WorldArea &world_area_of(render::RenderState &state) noexcept {
    auto *area = std::get_if<render::WorldArea>(&state.area);
    assert(area != nullptr);
    return *area;
  }

  /// @return The WorldArea alternative of @p state.
  /// @pre @p state is in World mode.
  [[nodiscard]] inline const render::WorldArea &world_area_of(const render::RenderState &state) noexcept {
    const auto *area = std::get_if<render::WorldArea>(&state.area);
    assert(area != nullptr);
    return *area;
  }

} // namespace corundum::test
