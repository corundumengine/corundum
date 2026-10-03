// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/screens/dialog_layout.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/panel_style.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace corundum::gameplay::screens {

  /// Reveal rate (codepoints per second) at text_speed 1.0, before the ui scale's font size
  /// adjustment; Text Speed presets multiply it.
  inline constexpr float k_base_reveal_chars_per_second = 45.f;

  /// All mutable dialogue-box state — pure data, no behaviour.
  ///
  /// Operated on by free functions in namespace corundum::gameplay::screens.
  struct DialogBoxState {
    bool visible{false};

    std::string last_graph_id;

    std::string last_node_id;

    /// Viewport the cached layout was built for; used to invalidate on resize.
    core::math::Vec2 last_viewport{};

    std::optional<DialogLayout> layout;

    /// Node the current reveal progress belongs to; a change resets reveal_chars.
    std::string reveal_node_id;

    /// Codepoints revealed so far for the current Talk body.
    float reveal_chars{};

    /// Whether the typewriter reveal is running; derived from text_speed on every update, so a
    /// settings change takes effect on the next frame with no stored rate to refresh.
    bool reveal_active{false};
  };

  /// Advance the typewriter reveal of the current Talk body by @p dt seconds.
  ///
  /// Resets the reveal when the conversation switches node, and is a no-op while the
  /// conversation is inactive or @p text_speed is zero (instant text). Called once per fixed
  /// step so reveal speed is independent of the render rate.
  ///
  /// @param text_speed RenderState::text_speed multiplier on k_base_reveal_chars_per_second;
  ///                   zero or less reveals instantly.
  void dialog_box_advance(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation, float dt,
                          float text_speed);

  /// Recompute layout if the current node, graph, viewport, or visible-choice set changed, then mark visible.
  /// Hides the box (leaving the cache intact) when the conversation is inactive.
  ///
  /// @param skin       Panel style and border the layout measures against.
  /// @param text_speed RenderState::text_speed multiplier; zero or less reveals instantly.
  void dialog_box_update(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation,
                         platform::Renderer &r, core::math::Vec2 viewport, const ui::PanelSkin &skin, float text_speed);

  /// Emit platform::DrawRect, nine-patch border, and platform::DrawText commands for the current frame.
  /// No-op when ds.visible is false or layout is absent.
  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r, const ui::PanelSkin &skin);

  /// Hide the box without clearing the cached layout.
  inline void dialog_box_hide(DialogBoxState &ds) noexcept {
    ds.visible = false;
  }

  /// True while the dialogue state that produced the last update() is active.
  [[nodiscard]] inline bool dialog_box_visible(const DialogBoxState &ds) noexcept {
    return ds.visible;
  }

} // namespace corundum::gameplay::screens
