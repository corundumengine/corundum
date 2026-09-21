// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/dialog_layout.hpp>

#include <corundum/ui/ui_draw.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

namespace corundum::ui {

  void dialog_box_update(DialogBoxState &ds, const dialogue::Conversation &conversation, platform::Renderer &r,
                         core::math::Vec2 viewport) {
    if (!conversation.is_active()) {
      ds.visible = false;
      return;
    }

    const std::string_view graph_id = conversation.graph_id();

    // Choice visibility is condition-evaluated (flags/quests), so a node visited twice can
    // present a different set of choices. The cache key must include it even when the graph,
    // node, and viewport are unchanged — e.g. ending and restarting the same graph after
    // quest progress, or a goto_graph loop back through this node.
    const bool choices_changed = ds.layout && conversation.node_type() == dialogue::NodeType::Choice &&
                                 conversation.visible_choice_indices() != ds.layout->choice_indices;

    const bool stale = !ds.layout || conversation.current_node_id() != ds.last_node_id ||
                       graph_id != ds.last_graph_id || viewport.x != ds.last_viewport.x ||
                       viewport.y != ds.last_viewport.y || choices_changed;

    if (stale) {
      const auto measure = [&](std::string_view text) -> float {
        return r.measure_text(ds.style.font_id, text, ds.style.font_size_body);
      };

      ds.layout =
          build_layout(conversation, ds.style.margin, ds.style.panel_height_frac, ds.border.tile_w, viewport, measure);
      ds.last_graph_id = graph_id;
      ds.last_node_id = conversation.current_node_id();
      ds.last_viewport = viewport;
    } else {
      ds.layout->selected_choice = conversation.selected_choice();
    }

    ds.visible = true;
  }

  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r) {
    if (!ds.visible || !ds.layout)
      return;

    const DialogLayout &lay = *ds.layout;
    const float px = lay.panel_pos.x;
    const float py = lay.panel_pos.y;
    const float inset = lay.inset;
    const float spacing = ds.style.line_spacing;

    panel_chrome(r, ds.style.bg, ds.border, lay.panel_pos, lay.panel_size);

    const auto draw_str = [&](std::string_view text, unsigned size, core::math::Colour col, float x, float y) {
      if (!text.empty())
        r.draw(platform::DrawText{
            .font_id = ds.style.font_id,
            .text = text,
            .position = {.x = x, .y = y},
            .char_size = size,
            .colour = col,
        });
    };

    switch (lay.node_type) {
      case dialogue::NodeType::Talk: {
        draw_str(lay.speaker, ds.style.font_size_speaker, ds.style.speaker, px + inset, py + inset);
        float y = py + inset + spacing;
        for (const auto &line : lay.body_lines) {
          if (line.empty()) {
            y += spacing;
            continue;
          }
          draw_str(line, ds.style.font_size_body, ds.style.body, px + inset, y);
          y += spacing;
        }
        draw_str("[Select] Continue   [Cancel] Close", ds.style.font_size_prompt, ds.style.choice, px + inset,
                 y + (spacing / 2.f));
        break;
      }
      case dialogue::NodeType::Choice: {
        const std::string_view header = lay.speaker.empty() ? std::string_view{"Choose:"} : lay.speaker;
        draw_str(header, ds.style.font_size_speaker, ds.style.speaker, px + inset, py + inset);
        for (std::size_t i = 0; i < lay.choice_lines.size(); ++i) {
          const bool is_sel = std::cmp_equal(i, lay.selected_choice);
          const float y = py + inset + (spacing * (1.f + static_cast<float>(i)));
          draw_option(r, ds.style, lay.choice_lines[i], {.x = px + inset, .y = y}, is_sel);
        }
        break;
      }
      case dialogue::NodeType::End:
        draw_str("[Select] Close", ds.style.font_size_body, ds.style.choice, px + inset, py + inset);
        break;
      case dialogue::NodeType::Event:
        break;
      default:
        std::unreachable();
    }
  }

} // namespace corundum::ui
