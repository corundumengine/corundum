// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/actions.hpp>
#include <corundum/input/input_system.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/window.hpp>

namespace corundum::input {

  void poll(InputMapper &mapper, InputState &state, platform::Window &window,
            platform::PlatformEvents &events) noexcept {
    mapper.begin_poll();
    window.poll_game_input(mapper, events);
    mapper.end_poll(state);
  }

} // namespace corundum::input
