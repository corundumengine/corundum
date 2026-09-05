#pragma once
#include <algorithm>
#include <corundum/core/math/vec.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/dialogue/query.hpp>
#include <corundum/ui/word_wrap.hpp>
#include <corundum/world/flags.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  /// Computed layout for one dialogue frame. Pure data — no renderer dependency.
  struct DialogLayout {
    core::math::Vec2 panel_pos{};
    core::math::Vec2 panel_size{};
    float inset{0.f};
    // NOLINTBEGIN(readability-redundant-member-init)
    // These default member initializers are required: build_layout designated-initializes
    // DialogLayout, and -Wmissing-designated-field-initializers (compiled with -Werror) demands
    // every field carry an explicit default initializer.
    std::string_view speaker{};                ///< Graph-level speaker name.
    std::vector<std::string> body_lines{};     ///< Wrapped body text (Talk nodes).
    std::vector<std::string> choice_lines{};   ///< One label per visible choice.
    std::vector<std::size_t> choice_indices{}; ///< choice_lines[i] → node.choices[j].
    // NOLINTEND(readability-redundant-member-init)
    int selected_choice{0};
    dialogue::NodeType node_type{dialogue::NodeType::End};
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
  /// @param quests        Quest registry for quest-gated choice conditions. Optional —
  ///                      defaults to nullptr; null is safe and renders all quest-gated
  ///                      choices as not-satisfied.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  // margin/panel_height_frac are both viewport-scaled floats; a value struct would over-abstract
  // this single call site.
  template <typename MeasureFn>
  [[nodiscard]] DialogLayout build_layout(const dialogue::State &state, const corundum::world::FlagStore &flags,
                                          float margin, float panel_height_frac, int border_tile_w,
                                          core::math::Vec2 viewport, MeasureFn measure,
                                          const quest::Registry *quests = nullptr) {
    const float panel_h = viewport.y * panel_height_frac;
    const float panel_y = viewport.y - panel_h - margin;
    const float panel_x = margin;
    const float panel_w = viewport.x - (margin * 2.f);
    const float inset = std::max(margin, static_cast<float>(border_tile_w));

    const dialogue::Node *node = state.graph->find(state.current_id);

    DialogLayout layout{
        .panel_pos = {.x = panel_x, .y = panel_y},
        .panel_size = {.x = panel_w, .y = panel_h},
        .inset = inset,
        .selected_choice = state.selected_choice,
        .node_type = node ? node->type : dialogue::NodeType::End,
    };

    if (!node)
      return layout;

    const float text_w = panel_w - (inset * 2.f);

    layout.speaker = state.graph->speaker;

    if (node->type == dialogue::NodeType::Talk) {
      layout.body_lines = ui::wrap_text(node->text, text_w, measure);
    } else if (node->type == dialogue::NodeType::Choice) {
      const float choice_w = panel_w - (inset * 3.f);
      layout.choice_indices = dialogue::visible_choices(*node, flags, state.graph->graph_id, quests);
      layout.choice_lines.reserve(layout.choice_indices.size());
      for (const std::size_t idx : layout.choice_indices) {
        auto lines = ui::wrap_text(node->choices[idx].label, choice_w, measure);
        layout.choice_lines.push_back(lines.empty() ? std::string{} : std::move(lines.front()));
      }
    }

    return layout;
  }

  // NOLINTEND(bugprone-easily-swappable-parameters)

} // namespace corundum::ui
