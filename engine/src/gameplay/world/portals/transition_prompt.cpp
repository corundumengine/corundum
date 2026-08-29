#include <corundum/gameplay/world/portals/transition_prompt.hpp>

#include <corundum/input/actions.hpp>

namespace corundum::gameplay::world {

  TransitionPrompt::TransitionPrompt(const Portal &portal) noexcept
      : rect_col_{portal.col}, rect_h_{portal.h}, rect_row_{portal.row}, rect_w_{portal.w},
        transition_{portal.target_map, portal.spawn_col, portal.spawn_row, portal.return_to_world} {}

  bool TransitionPrompt::overlaps(float col0, float col1, float row0, float row1) const noexcept {
    return col1 > rect_col_ && col0 < rect_col_ + rect_w_ && row1 > rect_row_ && row0 < rect_row_ + rect_h_;
  }

  bool TransitionPrompt::guards(const Portal &portal) const noexcept {
    return rect_col_ == portal.col && rect_row_ == portal.row && rect_w_ == portal.w && rect_h_ == portal.h;
  }

  TransitionPrompt::Step TransitionPrompt::step(const corundum::input::InputState &input) noexcept {
    using corundum::input::Action;

    // Left/Up → Yes, Right/Down → No. Only two options, so no wrap needed.
    if (input.is_pressed(Action::MoveLeft) || input.is_pressed(Action::MoveUp))
      confirm_selected_ = true;
    if (input.is_pressed(Action::MoveRight) || input.is_pressed(Action::MoveDown))
      confirm_selected_ = false;

    if (input.is_pressed(Action::Select)) {
      if (confirm_selected_)
        return Step::Confirmed;
      declined_ = true;
      return Step::Dismissed;
    }
    if (input.is_pressed(Action::Cancel)) {
      declined_ = true;
      return Step::Dismissed;
    }
    return Step::Pending;
  }

} // namespace corundum::gameplay::world
