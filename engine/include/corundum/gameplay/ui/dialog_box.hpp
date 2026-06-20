#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/ui/dialog_layout.hpp>
#include <corundum/gameplay/ui/nine_patch.hpp>
#include <corundum/platform/renderer.hpp>
#include <optional>
#include <string>

namespace corundum::gameplay::ui {

  /// Visual configuration for a dialogue box. Pure data — no behaviour.
  struct DialogBoxStyle {
    uint32_t font_id{0};
    unsigned font_size_speaker{26};
    unsigned font_size_body{22};
    unsigned font_size_prompt{18};
    float margin{20.f};
    float line_spacing{32.f};
    float panel_height_frac{0.32f};
    core::math::Colour bg{0, 0, 0, 200};
    core::math::Colour speaker{255, 255, 0, 255};
    core::math::Colour body{255, 255, 255, 255};
    core::math::Colour choice{200, 200, 200, 255};
    core::math::Colour selected{255, 255, 0, 255};
  };

  /// All mutable dialogue-box state — pure data, no behaviour.
  ///
  /// Operated on by free functions in namespace corundum::gameplay::ui.
  struct DialogBoxState {
    DialogBoxStyle style{};
    NinePatchBorder border{};
    bool visible{false};
    std::string last_node_id{};
    float last_panel_w{0.f};
    std::optional<DialogLayout> layout{};
  };

  /// Recompute layout if the current node or panel dimensions changed, then mark visible.
  /// @pre state.active && state.graph != nullptr when called.
  void dialog_box_update(DialogBoxState &ds, const gameplay::dialogue::State &state, const gameplay::FlagStore &flags,
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

} // namespace corundum::gameplay::ui
