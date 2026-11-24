// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "editor_state.hpp"
#include <corundum/toolkit/editor/canvas_controller.hpp>
#include <functional>

namespace tools::tilesmith {

using corundum::toolkit::editor::CanvasContext;

/// Injected callback that renders all tilemap passes into the canvas.
/// Constructed in main.cpp, which owns TilemapTextureStore.
using MapRenderFn = std::function<void(CanvasContext)>;

/** Render the tilemap canvas including collision and portal overlays.
 *
 *  Calls `render_map` to draw the tilemap passes (provided by main.cpp as a
 *  closure over TilemapTextureStore), then draws editor overlays.
 *
 *  @param ctx         Canvas draw context (draw list + screen-space origin).
 *  @param state       Current editor state.
 *  @param render_map  Callback that issues all tilemap draw calls into `ctx`.
 */
void render_canvas(CanvasContext ctx, const EditorState &state, const MapRenderFn &render_map);

} // namespace tools::tilesmith
