#include <corundum/input/input_sys.hpp>
#include <corundum/platform/window.hpp>

namespace corundum::input {

  void poll(corundum::input::InputState &state, corundum::platform::Window &window) noexcept {
    window.poll_game_input(state);
  }

} // namespace corundum::input
