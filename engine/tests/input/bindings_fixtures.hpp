// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>

#include <algorithm>
#include <vector>

/// Binding-table query helpers shared by the engine and gameplay suites. Every helper is `inline`
/// so translation units in both test binaries share one definition.
namespace corundum::test {

  /// @p action's inputs on @p device, in row order.
  [[nodiscard]] inline std::vector<input::PhysicalInput> inputs_on_device(const input::Bindings &bindings,
                                                                          input::Action action, input::InputDevice device) {
    std::vector<input::PhysicalInput> inputs = input::inputs_for(bindings, action);
    std::erase_if(inputs, [device](input::PhysicalInput input) { return input::device_of(input) != device; });
    return inputs;
  }

  /// @p action's gamepad inputs, in row order.
  [[nodiscard]] inline std::vector<input::PhysicalInput> gamepad_inputs(const input::Bindings &bindings,
                                                                        input::Action action) {
    return inputs_on_device(bindings, action, input::InputDevice::Gamepad);
  }

} // namespace corundum::test
