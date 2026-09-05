#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/dialogue/dialogue.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_layout.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>
#include <optional>
#include <string>

namespace corundum::quest {
  class Registry;
}

namespace corundum::ui {

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
    float last_panel_w{0.f};
    std::optional<DialogLayout> layout;
  };

  /// Recompute layout if the current node, graph, or panel dimensions changed, then mark visible.
  /// @pre state.active && state.graph != nullptr when called.
  /// @param quests Quest registry used by visible_choices to evaluate quest-gated conditions.
  void dialog_box_update(DialogBoxState &ds, const dialogue::State &state, const corundum::world::FlagStore &flags,
                         const quest::Registry *quests, platform::Renderer &r, core::math::Vec2 viewport);

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
