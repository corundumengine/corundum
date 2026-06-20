#pragma once

namespace tools::common {

  /// Mouse button state sampled once per frame.
  struct MouseState {
    bool left_held{};  ///< True while the left mouse button is held.
    bool right_held{}; ///< True while the right mouse button is held.
  };

} // namespace tools::common
