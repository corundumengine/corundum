// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <corundum/core/math/vec.hpp>
#include <corundum/dialogue/conversation.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/ui/choice_cursor.hpp>
#include <corundum/ui/word_wrap.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  /// One visible dialogue choice: the index into the node's full choice list and its label
  /// pre-wrapped into the lines the renderer draws in order (at least one, possibly empty).
  struct ChoiceLayout {
    std::size_t index{0};

    std::vector<std::string> lines{};
  };

  /// Computed layout for one dialogue frame. Pure data — no renderer dependency.
  ///
  /// Every field carries a default member initializer: build_layout designated-initializes
  /// DialogLayout, and -Wmissing-designated-field-initializers (compiled with -Werror)
  /// requires each field to have one.
  struct DialogLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    float inset{0.f};

    std::string_view speaker{};

    std::vector<std::string> body_lines{};

    std::vector<ChoiceLayout> choices{};

    int selected_choice{0};

    dialogue::NodeType node_type{dialogue::NodeType::End};
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
  /// @param margin        Panel margin in pixels; also the minimum inset, so text clears the border.
  /// @param panel_height_frac Fraction of the viewport height for the panel.
  /// @param border_tile_w Tile width of the nine-patch border (determines inset).
  /// @param viewport      Viewport dimensions in pixels.
  /// @param measure       Callable (std::string_view) -> float returning rendered width.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // margin/panel_height_frac are both viewport-scaled floats; a value struct would over-abstract
  // this single call site.
  template <typename MeasureFn>
  [[nodiscard]] DialogLayout build_layout(const dialogue::Conversation &conversation, float margin,
                                          float panel_height_frac, int border_tile_w, core::math::Vec2 viewport,
                                          const MeasureFn &measure) {
    const float panel_h = viewport.y * panel_height_frac;
    const float panel_y = viewport.y - panel_h - margin;
    const float panel_x = margin;
    const float panel_w = viewport.x - (margin * 2.f);
    const float inset = std::max(margin, static_cast<float>(border_tile_w));

    const dialogue::NodeType type = conversation.node_type();

    DialogLayout layout{
        .panel_pos = {.x = panel_x, .y = panel_y},
        .panel_size = {.x = panel_w, .y = panel_h},
        .inset = inset,
        .selected_choice = conversation.selected_choice(),
        .node_type = type,
    };

    layout.speaker = conversation.speaker();

    const float text_w = std::max(0.f, panel_w - (inset * 2.f));
    if (type == dialogue::NodeType::Talk) {
      layout.body_lines = ui::wrap_text(conversation.current_text(), text_w, measure);
    } else if (type == dialogue::NodeType::Choice) {
      // The cursor column lives inside the text width; measuring it keeps the wrap budget
      // equal to the label's actual drawable width.
      const float choice_w = std::max(0.f, text_w - measure(k_choice_cursor));
      const std::vector<std::size_t> indices = conversation.visible_choice_indices();
      layout.choices.reserve(indices.size());
      for (const std::size_t index : indices) {
        std::vector<std::string> lines = ui::wrap_text(conversation.choice_label(index), choice_w, measure);
        if (lines.empty())
          lines.emplace_back();
        layout.choices.push_back(ChoiceLayout{.index = index, .lines = std::move(lines)});
      }
    }

    return layout;
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace corundum::ui
