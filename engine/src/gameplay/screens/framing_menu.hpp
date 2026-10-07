// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// Shared geometry and drawing for the framing screens (Title, Game over): a centered panel
// over an opaque backdrop with a heading, a uniform column of rows and a footer hint. Title
// and Game over differ only in their row labels and which rows are enabled, so both defer to
// this file rather than maintaining two near-identical panel builders.

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <cstddef>
#include <span>
#include <string_view>

namespace corundum::gameplay::screens::detail {

  /** @brief Screen-space geometry of a framing menu: the centered panel and its row hit rects. */
  struct FramingMenuLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    ui::ListHit rows{};
  };

  /** @brief Compute a framing menu's panel and row geometry for @p viewport.
   *
   *  The panel is centered and wide enough for @p heading, @p footer and the widest @p label.
   *  Row height is the panel style's line spacing; the row count is `labels.size()`.
   */
  [[nodiscard]] FramingMenuLayout framing_menu_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                      core::math::Vec2 viewport,
                                                      std::span<const std::string_view> labels,
                                                      std::string_view heading, std::string_view footer);

  /** @brief Draw a framing menu: opaque backdrop, panel chrome, heading, rows and footer.
   *
   *  @param labels   Row labels, in draw order; must match the layout's row count.
   *  @param enabled  Parallel to @p labels; a disabled row never draws the selection cursor.
   *  @param cursor   Highlighted row index; clamped into range locally.
   *  @param viewport Screen size in logical pixels, filled by the opaque backdrop.
   */
  void framing_menu_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                           const FramingMenuLayout &layout, std::span<const std::string_view> labels,
                           std::span<const bool> enabled, int cursor, std::string_view heading, std::string_view footer,
                           core::math::Vec2 viewport);

} // namespace corundum::gameplay::screens::detail
