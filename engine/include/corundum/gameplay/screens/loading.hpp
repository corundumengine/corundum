// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>

namespace corundum::gameplay::screens {

  /** @brief Draw the two-phase loading overlay: an opaque backdrop with a centered "Loading…".
   *
   *  Pure render. Its only job is to put something on screen before Gameplay::begin_load()'s
   *  queued work runs, so the player never sees an unpainted frame during a load. See
   *  Gameplay::begin_load() for the render-then-run protocol.
   *
   *  @param r        Renderer; emits the backdrop and one line of text.
   *  @param style    Panel style supplying fonts, sizes and colours.
   *  @param viewport Screen size in logical pixels.
   */
  void loading_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport);

} // namespace corundum::gameplay::screens
