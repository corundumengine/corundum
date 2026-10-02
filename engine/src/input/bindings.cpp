// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/input/bindings.hpp>

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::input {

  namespace {

    constexpr std::array<std::pair<Action, std::string_view>, k_action_count> k_action_names{
        {
            {Action::MoveUp, "MoveUp"},
            {Action::MoveDown, "MoveDown"},
            {Action::MoveLeft, "MoveLeft"},
            {Action::MoveRight, "MoveRight"},
            {Action::Select, "Select"},
            {Action::Activate, "Activate"},
            {Action::Cancel, "Cancel"},
            {Action::Quit, "Quit"},
            {Action::ZoomIn, "ZoomIn"},
            {Action::ZoomOut, "ZoomOut"},
            {Action::Inventory, "Inventory"},
            {Action::Journal, "Journal"},
            {Action::QuickSave, "QuickSave"},
            {Action::QuickLoad, "QuickLoad"},
        },
    };

  } // namespace

  Bindings default_bindings() {
    return {
        {.action = Action::MoveUp, .input = physical(Key::W)},
        {.action = Action::MoveDown, .input = physical(Key::S)},
        {.action = Action::MoveLeft, .input = physical(Key::A)},
        {.action = Action::MoveRight, .input = physical(Key::D)},
        {.action = Action::MoveUp, .input = physical(Key::Up)},
        {.action = Action::MoveDown, .input = physical(Key::Down)},
        {.action = Action::MoveLeft, .input = physical(Key::Left)},
        {.action = Action::MoveRight, .input = physical(Key::Right)},
        {.action = Action::Select, .input = physical(Key::Enter)},
        {.action = Action::Select, .input = physical(Key::Space)},
        {.action = Action::Activate, .input = physical(Key::Enter)},
        {.action = Action::Activate, .input = physical(Key::Space)},
        {.action = Action::Cancel, .input = physical(Key::Escape)},
        {.action = Action::Quit, .input = physical(Key::Q)},
        {.action = Action::Inventory, .input = physical(Key::I)},
        {.action = Action::Journal, .input = physical(Key::J)},
        {.action = Action::ZoomIn, .input = physical(Key::Equal)}, // '=' doubles as '+' without needing Shift
        {.action = Action::ZoomOut, .input = physical(Key::Minus)},
        {.action = Action::QuickSave, .input = physical(Key::F5)},
        {.action = Action::QuickLoad, .input = physical(Key::F9)},
        {.action = Action::Select, .input = physical(MouseButton::Left)},
        {.action = Action::Select, .input = physical(GamepadControl::A)},
        {.action = Action::Activate, .input = physical(MouseButton::Left)},
        {.action = Action::Activate, .input = physical(GamepadControl::A)},
        {.action = Action::Cancel, .input = physical(GamepadControl::B)},
        {.action = Action::Journal, .input = physical(GamepadControl::Y)},
        {.action = Action::MoveUp, .input = physical(GamepadControl::LeftStickUp)},
        {.action = Action::MoveUp, .input = physical(GamepadControl::DpadUp)},
        {.action = Action::MoveDown, .input = physical(GamepadControl::LeftStickDown)},
        {.action = Action::MoveDown, .input = physical(GamepadControl::DpadDown)},
        {.action = Action::MoveLeft, .input = physical(GamepadControl::LeftStickLeft)},
        {.action = Action::MoveLeft, .input = physical(GamepadControl::DpadLeft)},
        {.action = Action::MoveRight, .input = physical(GamepadControl::LeftStickRight)},
        {.action = Action::MoveRight, .input = physical(GamepadControl::DpadRight)},
        {.action = Action::ZoomOut, .input = physical(GamepadControl::LeftTrigger)}, // L2
        {.action = Action::ZoomIn, .input = physical(GamepadControl::RightTrigger)}, // R2
    };
  }

  std::string_view action_name(Action action) noexcept {
    for (const auto &[candidate, name] : k_action_names) {
      if (candidate == action)
        return name;
    }
    return {};
  }

  std::optional<Action> parse_action(std::string_view name) noexcept {
    for (const auto &[action, candidate] : k_action_names) {
      if (candidate == name)
        return action;
    }
    return std::nullopt;
  }

  std::vector<PhysicalInput> inputs_for(const Bindings &bindings, Action action) {
    std::vector<PhysicalInput> result;
    for (const Binding &row : bindings) {
      if (row.action == action)
        result.push_back(row.input);
    }
    return result;
  }

  void bind(Bindings &bindings, Action action, PhysicalInput input) {
    std::erase_if(bindings, [input](const Binding &row) { return row.input == input; });
    bindings.push_back(Binding{.action = action, .input = input});
  }

  void rebind(Bindings &bindings, Action action, PhysicalInput from, PhysicalInput to) {
    if (from == to)
      return;

    std::erase_if(bindings, [to](const Binding &row) { return row.input == to; });

    const auto target = std::ranges::find_if(
        bindings, [action, from](const Binding &row) { return row.action == action && row.input == from; });
    if (target != bindings.end()) {
      target->input = to;
      return;
    }
    bindings.push_back(Binding{.action = action, .input = to});
  }

  void unbind(Bindings &bindings, Action action, PhysicalInput input) {
    std::erase_if(bindings, [action, input](const Binding &row) { return row.action == action && row.input == input; });
  }

  nlohmann::json serialize(const Bindings &bindings) {
    nlohmann::json array = nlohmann::json::array();
    for (const Binding &row : bindings) {
      array.push_back({
          {"action", std::string(action_name(row.action))},
          {"device", std::string(device_name(row.input.device))},
          {"input", std::string(name_of(row.input))},
      });
    }
    return array;
  }

  namespace {

    /// Why one entry of the serialized array could not be read; empty when it was.
    std::string parse_entry_error(const nlohmann::json &entry, std::size_t index) {
      if (!entry.is_object() || !entry.contains("action") || !entry["action"].is_string() ||
          !entry.contains("device") || !entry["device"].is_string() || !entry.contains("input") ||
          !entry["input"].is_string()) {
        return std::format("bindings[{}]: must be an object with string fields action, device, input", index);
      }
      return {};
    }

  } // namespace

  std::expected<Bindings, std::string> parse_bindings(const nlohmann::json &array, const Bindings &defaults) {
    if (!array.is_array())
      return std::unexpected("bindings: must be an array");

    Bindings result;
    for (std::size_t index = 0; index < array.size(); ++index) {
      const nlohmann::json &entry = array[index];
      if (const std::string error = parse_entry_error(entry, index); !error.empty())
        return std::unexpected(error);

      const std::string action_text = entry["action"].get<std::string>();
      const std::string device_text = entry["device"].get<std::string>();
      const std::string input_text = entry["input"].get<std::string>();

      const std::optional<Action> action = parse_action(action_text);
      if (!action)
        return std::unexpected(std::format("bindings[{}]: unknown action '{}'", index, action_text));

      const std::optional<InputDevice> device = parse_device(device_text);
      if (!device)
        return std::unexpected(std::format("bindings[{}]: unknown device '{}'", index, device_text));

      const std::optional<PhysicalInput> input = parse_physical_input(*device, input_text);
      if (!input)
        return std::unexpected(std::format("bindings[{}]: unknown input '{}'", index, input_text));

      const Binding row{.action = *action, .input = *input};
      if (std::ranges::find(result, row) == result.end())
        result.push_back(row);
    }

    for (std::size_t action_index = 0; action_index < k_action_count; ++action_index) {
      const auto action = static_cast<Action>(action_index);
      const bool present = std::ranges::any_of(result, [action](const Binding &row) { return row.action == action; });
      if (present)
        continue;

      for (const Binding &default_row : defaults) {
        if (default_row.action != action)
          continue;
        const bool input_bound =
            std::ranges::any_of(result, [&default_row](const Binding &row) { return row.input == default_row.input; });
        if (!input_bound)
          result.push_back(default_row);
      }
    }

    if (result.size() > k_max_input_sources)
      return std::unexpected(
          std::format("binding table has {} rows; the limit is {}", result.size(), k_max_input_sources));

    return result;
  }

} // namespace corundum::input
