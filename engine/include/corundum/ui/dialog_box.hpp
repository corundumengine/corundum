// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_layout.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace corundum::ui {

  /// Reveal rate (codepoints per second) at text_speed 1.0, before the ui scale's font size
  /// adjustment; Text Speed presets multiply it.
  inline constexpr float k_base_reveal_chars_per_second = 45.f;

  /// Visual configuration for a dialogue box. Pure data — no behaviour.
  struct DialogBoxStyle {
    uint32_t font_id{0};

    unsigned font_size_speaker{26};

    unsigned font_size_body{22};

    unsigned font_size_prompt{18};

    float margin{20.f};

    float line_spacing{32.f};

    float panel_height_frac{0.32f};

    core::math::Colour bg{.r = 0, .g = 0, .b = 0, .a = 200};

    core::math::Colour speaker{.r = 255, .g = 255, .b = 0, .a = 255};

    core::math::Colour body{.r = 255, .g = 255, .b = 255, .a = 255};

    core::math::Colour choice{.r = 200, .g = 200, .b = 200, .a = 255};

    core::math::Colour selected{.r = 255, .g = 255, .b = 0, .a = 255};
  };

  /// All mutable dialogue-box state — pure data, no behaviour.
  ///
  /// Operated on by free functions in namespace corundum::ui.
  struct DialogBoxState {
    DialogBoxStyle style{};

    NinePatchBorder border{};

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

    /// Reveal rate in codepoints per second; 0 or less reveals instantly. Set from
    /// RenderState::text_speed by configure_dialog_style().
    float reveal_chars_per_second{};
  };

  /// Advance the typewriter reveal of the current Talk body by @p dt seconds.
  ///
  /// Resets the reveal when the conversation switches node, and is a no-op while the
  /// conversation is inactive or the reveal rate is zero (instant text). Called once per fixed
  /// step so reveal speed is independent of the render rate.
  void dialog_box_advance(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation, float dt);

  /// Recompute layout if the current node, graph, viewport, or visible-choice set changed, then mark visible.
  /// Hides the box (leaving the cache intact) when the conversation is inactive.
  void dialog_box_update(DialogBoxState &ds, const gameplay::dialogue::Conversation &conversation,
                         platform::Renderer &r, core::math::Vec2 viewport);

  /// Emit platform::DrawRect, nine-patch border, and platform::DrawText commands for the current frame.
  /// No-op when ds.visible is false or layout is absent.
  void dialog_box_render(const DialogBoxState &ds, platform::Renderer &r);

  /// Hide the box without clearing the cached layout.
  inline void dialog_box_hide(DialogBoxState &ds) noexcept {
    ds.visible = false;
  }

  /// True while the dialogue state that produced the last update() is active.
  [[nodiscard]] inline bool dialog_box_visible(const DialogBoxState &ds) noexcept {
    return ds.visible;
  }

} // namespace corundum::ui
