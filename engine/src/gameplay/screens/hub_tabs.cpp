// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/modes.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/ui_stack.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace corundum::gameplay::screens {

  namespace {

    constexpr float k_top_margin = 12.f;
    constexpr float k_tab_gap = 22.f;
    constexpr float k_glyph_gap = 16.f;

  } // namespace

  std::string_view hub_tab_label(world::GameMode mode) noexcept {
    switch (mode) {
      case Inventory:
        return "Inventory";
      case Journal:
        return "Journal";
      case Codex:
        return "Codex";
      case Map:
        return "Map";
      default:
        return {};
    }
  }

  HubTabStrip hub_tab_strip(const platform::Renderer &r, const ui::PanelStyle &style, core::math::Vec2 viewport) {
    HubTabStrip strip{};
    strip.line_height = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    strip.y = k_top_margin;

    std::array<float, k_hub_tab_modes.size()> widths{};
    float total = 0.f;
    for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
      widths[i] = r.measure_text(style.font_id, hub_tab_label(k_hub_tab_modes[i]), style.font_size_body);
      total += widths[i];
    }
    total += k_tab_gap * static_cast<float>(k_hub_tab_modes.size() - 1);

    float x = (viewport.x - total) * 0.5f;
    for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
      strip.tabs[i] = HubTabRect{.pos = {.x = x, .y = strip.y}, .width = widths[i]};
      x += widths[i] + k_tab_gap;
    }
    return strip;
  }

  void hub_tab_strip_render(platform::Renderer &r, const ui::PanelStyle &style, world::GameMode active,
                            core::math::Vec2 viewport, input::InputDevice last_device) {
    const HubTabStrip strip = hub_tab_strip(r, style, viewport);
    const std::string_view prev = ui::input_glyph(input::Action::TabPrev, last_device);
    const std::string_view next = ui::input_glyph(input::Action::TabNext, last_device);

    const float prev_w = r.measure_text(style.font_id, prev, style.font_size_prompt);
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = prev,
        .position = {.x = strip.tabs.front().pos.x - k_glyph_gap - prev_w, .y = strip.y},
        .char_size = style.font_size_prompt,
        .colour = style.choice,
    });

    for (std::size_t i = 0; i < k_hub_tab_modes.size(); ++i) {
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = hub_tab_label(k_hub_tab_modes[i]),
          .position = strip.tabs[i].pos,
          .char_size = style.font_size_body,
          .colour = k_hub_tab_modes[i] == active ? style.speaker : style.choice,
      });
    }

    const HubTabRect &last = strip.tabs.back();
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = next,
        .position = {.x = last.pos.x + last.width + k_glyph_gap, .y = strip.y},
        .char_size = style.font_size_prompt,
        .colour = style.choice,
    });
  }

} // namespace corundum::gameplay::screens
