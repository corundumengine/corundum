#pragma once
#include <algorithm>
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/query.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/word_wrap.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::ui {

  /// Computed layout for one dialogue frame. Pure data — no renderer dependency.
  struct DialogLayout {
    core::math::Vec2 panel_pos{};
    core::math::Vec2 panel_size{};
    float inset{0.f};
    std::string_view speaker{};                ///< Graph-level speaker name.
    std::vector<std::string> body_lines{};     ///< Wrapped body text (Talk nodes).
    std::vector<std::string> choice_lines{};   ///< One label per visible choice.
    std::vector<std::size_t> choice_indices{}; ///< choice_lines[i] → node.choices[j].
    int selected_choice{0};
    gameplay::dialogue::NodeType node_type{gameplay::dialogue::NodeType::End};
  };

  /// Builds a DialogLayout from the current dialogue state.
  ///
  /// The measure callable is the only coupling to font/platform — callers supply
  /// a lambda wrapping Renderer::measure_text, or a fixed stub for tests.
  ///
  /// @param state         Active dialogue session. @pre state.active && state.graph != nullptr.
  /// @param flags         FlagStore for visible_choices evaluation.
  /// @param margin        Panel margin in pixels.
  /// @param panel_height_frac Fraction of the viewport height for the panel.
  /// @param border_tile_w Tile width of the nine-patch border (determines inset).
  /// @param viewport      Viewport dimensions in pixels.
  /// @param measure       Callable (std::string_view) -> float returning rendered width.
  template <typename MeasureFn>
  [[nodiscard]] DialogLayout build_layout(const gameplay::dialogue::State &state, const gameplay::FlagStore &flags,
                                          float margin, float panel_height_frac, int border_tile_w,
                                          core::math::Vec2 viewport, MeasureFn measure) {
    const float panel_h = viewport.y * panel_height_frac;
    const float panel_y = viewport.y - panel_h - margin;
    const float panel_x = margin;
    const float panel_w = viewport.x - margin * 2.f;
    const float inset = std::max(margin, static_cast<float>(border_tile_w));

    const gameplay::dialogue::Node *node = state.graph->find(state.current_id);

    DialogLayout layout{
        .panel_pos = {panel_x, panel_y},
        .panel_size = {panel_w, panel_h},
        .inset = inset,
        .selected_choice = state.selected_choice,
        .node_type = node ? node->type : gameplay::dialogue::NodeType::End,
    };

    if (!node)
      return layout;

    const float text_w = panel_w - inset * 2.f;

    layout.speaker = state.graph->speaker;

    if (node->type == gameplay::dialogue::NodeType::Talk) {
      layout.body_lines = gameplay::wrap_text(node->text, text_w, measure);
    } else if (node->type == gameplay::dialogue::NodeType::Choice) {
      const float choice_w = panel_w - inset * 3.f;
      layout.choice_indices = gameplay::dialogue::visible_choices(*node, flags, state.graph->graph_id);
      layout.choice_lines.reserve(layout.choice_indices.size());
      for (const std::size_t idx : layout.choice_indices) {
        auto lines = gameplay::wrap_text(node->choices[idx].label, choice_w, measure);
        layout.choice_lines.push_back(lines.empty() ? std::string{} : std::move(lines.front()));
      }
    }

    return layout;
  }

} // namespace corundum::gameplay::ui
