// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/word_wrap_styled.hpp>
#include <cstddef>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /// One visible dialogue choice: the index into the node's full choice list and its label
  /// pre-wrapped into the lines the renderer draws in order (at least one, possibly empty).
  struct ChoiceLayout {
    std::size_t index{0};

    std::vector<ui::StyledLine> lines{};
  };

  /// Computed layout for one dialogue frame. Pure data — no renderer dependency.
  ///
  /// Every field carries a default member initializer: build_layout designated-initializes
  /// DialogLayout, and -Wmissing-designated-field-initializers (compiled with warnings-as-errors)
  /// requires each field to have one.
  struct DialogLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    float inset{0.f};

    std::string_view speaker{};

    std::vector<ui::StyledLine> body_lines{};

    std::vector<ChoiceLayout> choices{};

    int selected_choice{0};

    gameplay::dialogue::NodeType node_type{gameplay::dialogue::NodeType::End};
  };

  /// True when @p layout's choices carry @p indices in order. Labels are a pure function of
  /// the index, so comparing indices is enough to detect a changed visible-choice set.
  [[nodiscard]] inline bool choices_match(const DialogLayout &layout,
                                          const std::vector<std::size_t> &indices) noexcept {
    if (layout.choices.size() != indices.size())
      return false;

    for (std::size_t i = 0; i < indices.size(); ++i) {
      if (layout.choices[i].index != indices[i])
        return false;
    }

    return true;
  }

  /// Builds a DialogLayout from the current dialogue conversation.
  ///
  /// The measure callable is the only coupling to font/platform — callers supply
  /// a lambda wrapping Renderer::measure_text, or a fixed stub for tests.
  ///
  /// An inactive conversation yields an End-type layout with empty text, matching the
  /// values Conversation reports while inactive.
  ///
  /// @param conversation Active dialogue conversation.
  /// @param style         Panel style; its margin and panel_height_frac drive the frame.
  /// @param border_tile_w Tile width of the nine-patch border (determines inset).
  /// @param viewport      Viewport dimensions in pixels.
  /// @param measure       Callable (std::string_view, ui::FontStyle) -> float returning rendered width.
  template <typename MeasureFn>
  [[nodiscard]] DialogLayout build_layout(const gameplay::dialogue::Conversation &conversation,
                                          const ui::PanelStyle &style, int border_tile_w, core::math::Vec2 viewport,
                                          const MeasureFn &measure) {
    const float margin = style.margin;
    const float panel_h = viewport.y * style.panel_height_frac;
    const float panel_y = viewport.y - panel_h - margin;
    const float panel_x = margin;
    const float panel_w = viewport.x - (margin * 2.f);
    const float inset = std::max(margin, static_cast<float>(border_tile_w));

    const gameplay::dialogue::NodeType type = conversation.node_type();

    DialogLayout layout{
        .panel_pos = {.x = panel_x, .y = panel_y},
        .panel_size = {.x = panel_w, .y = panel_h},
        .inset = inset,
        .selected_choice = conversation.selected_choice(),
        .node_type = type,
    };

    layout.speaker = conversation.speaker();

    const float text_w = std::max(0.f, panel_w - (inset * 2.f));
    if (type == gameplay::dialogue::NodeType::Talk) {
      layout.body_lines = ui::wrap_styled(ui::parse_styled(conversation.current_text()), text_w, measure);
    } else if (type == gameplay::dialogue::NodeType::Choice) {
      // The cursor column lives inside the text width; measuring it keeps the wrap budget
      // equal to the label's actual drawable width.
      const float choice_w = std::max(0.f, text_w - measure(ui::k_choice_cursor, ui::FontStyle::Regular));
      const std::vector<std::size_t> indices = conversation.visible_choice_indices();
      layout.choices.reserve(indices.size());
      for (const std::size_t index : indices) {
        layout.choices.push_back(ChoiceLayout{
            .index = index,
            .lines = ui::wrap_styled(ui::parse_styled(conversation.choice_label(index)), choice_w, measure),
        });
      }
    }

    return layout;
  }

} // namespace corundum::gameplay::screens
