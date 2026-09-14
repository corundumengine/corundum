// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/actions.hpp>
#include <corundum/input/input_sys.hpp>
#include <corundum/platform/window.hpp>

namespace corundum::input {

  void poll(InputState &state, platform::Window &window) noexcept {
    window.poll_game_input(state);
  }

} // namespace corundum::input
