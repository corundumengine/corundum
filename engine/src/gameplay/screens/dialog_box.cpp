// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/core/utf8.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/dialog_layout.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace corundum::gameplay::screens {

  namespace {

    /// The longest prefix of @p text that fits @p budget codepoints, and how many codepoints
    /// that prefix consumed. Returns an empty prefix (and 0) when the budget is exhausted.
    std::pair<std::string_view, int> reveal_prefix(std::string_view text, int budget) {
      if (budget <= 0)
        return {std::string_view{}, 0};
      std::size_t offset = 0;
      int count = 0;
      while (offset < text.size() && count < budget) {
        (void)corundum::core::decode_utf8(text, offset);
        ++count;
      }
      return {text.substr(0, offset), count};
    }

  } // namespace

  void dialog_box_advance(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation, float dt,
                          float text_speed) {
    if (!conversation.is_active()) {
      ds.reveal_chars = 0.f;
      ds.reveal_node_id.clear();
      return;
    }
    if (const std::string_view node = conversation.current_node_id(); node != ds.reveal_node_id) {
      ds.reveal_node_id.assign(node);
      ds.reveal_chars = 0.f;
    }
    const float reveal_chars_per_second = k_base_reveal_chars_per_second * text_speed;
    if (reveal_chars_per_second > 0.f)
      ds.reveal_chars += reveal_chars_per_second * dt;
  }

  void dialog_box_update(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation,
                         platform::Renderer &r, core::math::Vec2 viewport, const ui::PanelSkin &skin,
                         float text_speed) {
    ds.reveal_active = k_base_reveal_chars_per_second * text_speed > 0.f;
    if (!conversation.is_active()) {
      ds.visible = false;
      return;
    }

    const std::string_view graph_id = conversation.graph_id();
    if (const std::string_view node = conversation.current_node_id(); node != ds.reveal_node_id) {
      ds.reveal_node_id.assign(node);
      ds.reveal_chars = 0.f;
    }

    // Choice visibility is condition-evaluated (flags/quests), so a node visited twice can
    // present a different set of choices. The cache key must include it even when the graph,
    // node, and viewport are unchanged — e.g. ending and restarting the same graph after
    // quest progress, or a goto_graph loop back through this node.
    const bool choices_changed = ds.layout && conversation.node_type() == gameplay::dialogue::NodeType::Choice &&
                                 !choices_match(*ds.layout, conversation.visible_choice_indices());

    const bool stale = !ds.layout || conversation.current_node_id() != ds.last_node_id ||
                       graph_id != ds.last_graph_id || viewport.x != ds.last_viewport.x ||
                       viewport.y != ds.last_viewport.y || choices_changed;

    if (stale) {
      const std::uint32_t font_id = skin.style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      const auto measure = [&](std::string_view text) -> float {
        return r.measure_text(font_id, text, skin.style.font_size_body);
      };

      ds.layout = build_layout(conversation, skin.style, skin.border.tile_w, viewport, measure);
      ds.last_graph_id = graph_id;
      ds.last_node_id = conversation.current_node_id();
      ds.last_viewport = viewport;
    } else {
      ds.layout->selected_choice = conversation.selected_choice();
    }

    ds.visible = true;
  }

  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r, const ui::PanelSkin &skin) {
    if (!ds.visible || !ds.layout)
      return;

    const DialogLayout &lay = *ds.layout;
    const float px = lay.panel_pos.x;
    const float py = lay.panel_pos.y;
    const float inset = lay.inset;
    const float spacing = skin.style.line_spacing;

    ui::panel_chrome(r, skin.style.bg, skin.border, lay.panel_pos, lay.panel_size);

    const std::uint32_t font_id = skin.style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
    const auto draw_str = [&](std::string_view text, unsigned size, core::math::Colour col, float x, float y) {
      if (!text.empty())
        r.draw(platform::DrawText{
            .font_id = font_id,
            .text = text,
            .position = {.x = x, .y = y},
            .char_size = size,
            .colour = col,
        });
    };

    switch (lay.node_type) {
      case gameplay::dialogue::NodeType::Talk: {
        draw_str(lay.speaker, skin.style.font_size_speaker, skin.style.speaker, px + inset, py + inset);
        float y = py + inset + spacing;
        int remaining = static_cast<int>(ds.reveal_chars);
        for (const auto &line : lay.body_lines) {
          if (line.empty()) {
            y += spacing;
            continue;
          }
          if (ds.reveal_active) {
            const auto [prefix, consumed] = reveal_prefix(line, remaining);
            remaining -= consumed;
            draw_str(prefix, skin.style.font_size_body, skin.style.body, px + inset, y);
          } else {
            draw_str(line, skin.style.font_size_body, skin.style.body, px + inset, y);
          }
          y += spacing;
        }
        draw_str("[Select] Continue   [Cancel] Close", skin.style.font_size_prompt, skin.style.choice, px + inset,
                 y + (spacing / 2.f));
        break;
      }
      case gameplay::dialogue::NodeType::Choice: {
        const std::string_view header = lay.speaker.empty() ? std::string_view{"Choose:"} : lay.speaker;
        draw_str(header, skin.style.font_size_speaker, skin.style.speaker, px + inset, py + inset);
        float y = py + inset + spacing;
        for (std::size_t i = 0; i < lay.choices.size(); ++i) {
          const bool is_sel = std::cmp_equal(i, lay.selected_choice);
          for (std::size_t line = 0; line < lay.choices[i].lines.size(); ++line) {
            // Every line keeps the selected colour; only the first carries the cursor, so
            // continuation lines are drawn as cursorless with the same hanging indent.
            ui::draw_option(r, skin.style, lay.choices[i].lines[line], {.x = px + inset, .y = y}, is_sel, line == 0);
            y += spacing;
          }
        }
        break;
      }
      case gameplay::dialogue::NodeType::End:
        draw_str("[Select] Close", skin.style.font_size_body, skin.style.choice, px + inset, py + inset);
        break;
      case gameplay::dialogue::NodeType::Event:
        break;
      default:
        std::unreachable();
    }
  }

} // namespace corundum::gameplay::screens
