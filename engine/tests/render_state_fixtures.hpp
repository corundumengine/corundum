// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "memory_content_source.hpp"
#include <corundum/render/render_state.hpp>
#include <corundum/world/tilemap/loader.hpp>

#include <cassert>
#include <variant>

namespace corundum::test {

/// A content source with no files. Immutable and process-lifetime, so it outlives every area
/// that reads it.
[[nodiscard]] inline const core::ContentSource &no_content() {
  static const MemoryContentSource k_no_content;
  return k_no_content;
}

/// Put @p state in World mode with an empty WorldArea, for a test that fills the chunk window by
/// hand. Streaming reads chunks from @p content, which by default has no files.
/// @pre @p content outlives the area.
inline render::WorldArea &emplace_world_area(render::RenderState &state,
                                             const core::ContentSource &content = no_content()) {
  return state.area.emplace<render::WorldArea>(
      render::WorldArea{.chunk_loader = world::tilemap::TilemapLoader{content}});
}

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
