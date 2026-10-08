// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/loading.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <cstdint>
#include <string_view>

namespace corundum::gameplay::screens {

  void loading_panel_render(platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport) {
    constexpr std::string_view k_loading_text = "Loading...";
    const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    ui::screen_backdrop(r, style, viewport);

    const float width = r.measure_text(font_id, k_loading_text, style.font_size_speaker);
    const float height = static_cast<float>(style.font_size_speaker);
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = k_loading_text,
        .position = {.x = (viewport.x - width) * 0.5f, .y = (viewport.y - height) * 0.5f},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
  }

} // namespace corundum::gameplay::screens
