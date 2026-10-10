// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/dialog_layout.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/ui/word_wrap_styled.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    /// Emits one DrawText in the Dialogue family's @p font_style. Empty text is skipped, so
    /// callers never special-case an absent speaker or an empty line.
    void draw_dialogue_text(platform::Renderer &r, const ui::PanelStyle &style, ui::FontStyle font_style,
                            std::string_view text, unsigned size, core::math::Colour colour, float x, float y) {
      if (text.empty())
        return;
      r.draw(platform::DrawText{
          .font_id = style.family(ui::FontRole::Dialogue).get(font_style),
          .text = text,
          .position = {.x = x, .y = y},
          .char_size = size,
          .colour = colour,
      });
    }

    /// Draws one wrapped segment, offset by its cumulative x within the line.
    void draw_dialogue_segment(platform::Renderer &r, const ui::PanelStyle &style, const ui::StyledSegment &segment,
                               unsigned size, core::math::Colour colour, float x, float y) {
      draw_dialogue_text(r, style, segment.style, segment.text, size, colour, x + segment.x, y);
    }

    /// Draws every segment of a wrapped — possibly partially revealed — line, preserving each
    /// run's style.
    void draw_segments(platform::Renderer &r, const ui::PanelStyle &style,
                       const std::vector<ui::StyledSegment> &segments, unsigned size, core::math::Colour colour,
                       float x, float y) {
      for (const ui::StyledSegment &segment : segments)
        draw_dialogue_segment(r, style, segment, size, colour, x, y);
    }

    /// Device-aware footer for a Talk node: Select continues, Cancel closes.
    std::string talk_footer(input::InputDevice last_device) {
      return std::format("{} Continue   {} Close", ui::input_glyph(input::Action::Select, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }

    /// Device-aware footer for an End node: Select closes.
    std::string end_footer(input::InputDevice last_device) {
      return std::format("{} Close", ui::input_glyph(input::Action::Select, last_device));
    }

    /// Draws the speaker header, the revealed body, and the continue prompt of a Talk node.
    void render_talk_body(const DialogLayout &layout, const DialogBoxState &ds, platform::Renderer &r,
                          const ui::PanelStyle &style, input::InputDevice last_device) {
      const float x = layout.panel_pos.x + layout.inset;
      draw_dialogue_text(r, style, ui::FontStyle::Bold, layout.speaker, style.font_size_speaker, style.speaker, x,
                         layout.panel_pos.y + layout.inset);

      float y = layout.panel_pos.y + layout.inset + style.line_spacing;
      int remaining = static_cast<int>(ds.reveal_chars);
      for (const ui::StyledLine &line : layout.body_lines) {
        if (line.segments.empty()) {
          y += style.line_spacing;
          continue;
        }
        if (ds.reveal_active) {
          const auto [revealed, consumed] = ui::reveal_prefix_styled(line.segments, remaining);
          remaining -= consumed;
          draw_segments(r, style, revealed, style.font_size_body, style.body, x, y);
        } else {
          draw_segments(r, style, line.segments, style.font_size_body, style.body, x, y);
        }
        y += style.line_spacing;
      }

      // The prompt is dialogue chrome, so it uses the Dialogue family like the body.
      draw_dialogue_text(r, style, ui::FontStyle::Regular, talk_footer(last_device), style.font_size_prompt,
                         style.choice, x, y + (style.line_spacing / 2.f));
    }

    /// Draws each visible choice, one StyledLine at a time, keeping the selected colour on
    /// continuation lines while only the first shows the cursor.
    void render_choices(const DialogLayout &layout, platform::Renderer &r, const ui::PanelStyle &style) {
      const float x = layout.panel_pos.x + layout.inset;
      const std::string_view header = layout.speaker.empty() ? std::string_view{"Choose:"} : layout.speaker;
      draw_dialogue_text(r, style, ui::FontStyle::Bold, header, style.font_size_speaker, style.speaker, x,
                         layout.panel_pos.y + layout.inset);

      // The cursor column must match the width build_layout reserved while wrapping, so it is
      // measured from the same Dialogue regular font rather than the UI role.
      const float advance = r.measure_text(style.family(ui::FontRole::Dialogue).get(ui::FontStyle::Regular),
                                           ui::k_choice_cursor, style.font_size_body);

      float y = layout.panel_pos.y + layout.inset + style.line_spacing;
      for (std::size_t i = 0; i < layout.choices.size(); ++i) {
        const bool is_selected = std::cmp_equal(i, layout.selected_choice);
        const core::math::Colour colour = is_selected ? style.selected : style.choice;
        const std::vector<ui::StyledLine> &lines = layout.choices[i].lines;
        for (std::size_t line = 0; line < lines.size(); ++line) {
          const std::string_view cursor = (is_selected && line == 0) ? ui::k_choice_cursor : ui::k_cursor_unselected;
          draw_dialogue_text(r, style, ui::FontStyle::Regular, cursor, style.font_size_body, colour, x, y);
          draw_segments(r, style, lines[line].segments, style.font_size_body, colour, x + advance, y);
          y += style.line_spacing;
        }
      }
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
      const ui::FontFamily &fonts = skin.style.family(ui::FontRole::Dialogue);
      const auto measure = [&](std::string_view text, ui::FontStyle style) -> float {
        return r.measure_text(fonts.get(style), text, skin.style.font_size_body);
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

  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r, const ui::PanelSkin &skin,
                         input::InputDevice last_device) {
    if (!ds.visible || !ds.layout)
      return;

    const DialogLayout &layout = *ds.layout;
    ui::panel_chrome(r, skin.style.bg, skin.border, layout.panel_pos, layout.panel_size);

    switch (layout.node_type) {
      case gameplay::dialogue::NodeType::Talk:
        render_talk_body(layout, ds, r, skin.style, last_device);
        break;
      case gameplay::dialogue::NodeType::Choice:
        render_choices(layout, r, skin.style);
        break;
      case gameplay::dialogue::NodeType::End:
        draw_dialogue_text(r, skin.style, ui::FontStyle::Regular, end_footer(last_device), skin.style.font_size_body,
                           skin.style.choice, layout.panel_pos.x + layout.inset, layout.panel_pos.y + layout.inset);
        break;
      case gameplay::dialogue::NodeType::Event:
        break;
      default:
        std::unreachable();
    }
  }

} // namespace corundum::gameplay::screens
