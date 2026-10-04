// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/input/action_resolver.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>

// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using corundum::input::Action;
using corundum::input::action_name;
using corundum::input::bind;
using corundum::input::Binding;
using corundum::input::Bindings;
using corundum::input::default_bindings;
using corundum::input::device_name;
using corundum::input::GamepadControl;
using corundum::input::InputDevice;
using corundum::input::inputs_for;
using corundum::input::k_action_count;
using corundum::input::k_max_input_sources;
using corundum::input::Key;
using corundum::input::name_of;
using corundum::input::parse_action;
using corundum::input::parse_bindings;
using corundum::input::physical;
using corundum::input::PhysicalInput;
using corundum::input::rebind;
using corundum::input::serialize;
using corundum::input::unbind;

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("bindings: defaults") {
  const Bindings defaults = default_bindings();
  CHECK(defaults.size() <= k_max_input_sources);
  CHECK(std::ranges::find(defaults, Binding{.action = Action::MoveUp, .input = physical(Key::W)}) != defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Select, .input = physical(GamepadControl::A)}) !=
        defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Journal, .input = physical(Key::J)}) != defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Hub, .input = physical(GamepadControl::Y)}) !=
        defaults.end());
  // The hub replaces the per-screen gamepad buttons: only Hub opens the four menu tabs on a pad.
  // Each hotkey keeps exactly its one keyboard binding and gains no gamepad row.
  const auto keyboard_only = [&](Action action) {
    const std::vector<PhysicalInput> inputs = inputs_for(defaults, action);
    return inputs.size() == 1 && inputs.front().device == InputDevice::Keyboard;
  };
  CHECK(keyboard_only(Action::Journal));
  CHECK(keyboard_only(Action::Codex));
  CHECK(keyboard_only(Action::Map));
  CHECK(keyboard_only(Action::Inventory));
  CHECK(std::ranges::find(defaults, Binding{.action = Action::SubTabPrev,
                                            .input = physical(GamepadControl::LeftTrigger)}) != defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::SubTabNext,
                                            .input = physical(GamepadControl::RightTrigger)}) != defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Activate, .input = physical(Key::Enter)}) !=
        defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Activate, .input = physical(GamepadControl::A)}) !=
        defaults.end());
  CHECK(std::ranges::find(defaults, Binding{.action = Action::Activate,
                                            .input = physical(corundum::input::MouseButton::Left)}) != defaults.end());

  const std::vector<PhysicalInput> expected{
      physical(Key::W),
      physical(Key::Up),
      physical(GamepadControl::LeftStickUp),
      physical(GamepadControl::DpadUp),
  };
  CHECK(inputs_for(defaults, Action::MoveUp) == expected);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("bindings: action names round-trip") {
  for (std::size_t i = 0; i < k_action_count; ++i) {
    const auto action = static_cast<Action>(i);
    const std::string_view name = action_name(action);
    REQUIRE_FALSE(name.empty());
    CHECK(parse_action(name) == action);
  }
  CHECK_FALSE(parse_action("Count").has_value());
  CHECK_FALSE(parse_action("Jump").has_value());
}

TEST_CASE("bindings: bind makes the input exclusive and appends") {
  Bindings bindings = default_bindings();
  bind(bindings, Action::Cancel, physical(Key::W));

  const std::vector<PhysicalInput> move_up = inputs_for(bindings, Action::MoveUp);
  CHECK(std::ranges::find(move_up, physical(Key::W)) == move_up.end());
  CHECK(bindings.back() == Binding{.action = Action::Cancel, .input = physical(Key::W)});
}

TEST_CASE("bindings: rebind keeps the row position and frees the old input") {
  Bindings bindings = default_bindings();
  const auto target = std::ranges::find(bindings, Binding{.action = Action::MoveUp, .input = physical(Key::W)});
  REQUIRE(target != bindings.end());
  const auto position = static_cast<std::size_t>(std::distance(bindings.begin(), target));

  rebind(bindings, Action::MoveUp, physical(Key::W), physical(Key::Q));

  CHECK(bindings[position] == Binding{.action = Action::MoveUp, .input = physical(Key::Q)});
  CHECK(std::ranges::find(bindings, Binding{.action = Action::Quit, .input = physical(Key::Q)}) == bindings.end());
}

TEST_CASE("bindings: rebinding an unbound input appends") {
  Bindings bindings = default_bindings();
  rebind(bindings, Action::MoveUp, physical(Key::K), physical(Key::J));

  CHECK(bindings.back() == Binding{.action = Action::MoveUp, .input = physical(Key::J)});
}

TEST_CASE("bindings: unbind removes only the matching row") {
  Bindings bindings = default_bindings();
  unbind(bindings, Action::MoveUp, physical(Key::W));

  const std::vector<PhysicalInput> move_up = inputs_for(bindings, Action::MoveUp);
  CHECK(std::ranges::find(move_up, physical(Key::W)) == move_up.end());
  CHECK(std::ranges::find(move_up, physical(Key::Up)) != move_up.end());
}

TEST_CASE("bindings: serialize/parse round-trips") {
  const Bindings defaults = default_bindings();
  const auto parsed = parse_bindings(serialize(defaults), defaults);

  REQUIRE(parsed.has_value());
  CHECK(*parsed == defaults);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("bindings: parsing fills action gaps from defaults without duplicating inputs") {
  const Bindings defaults = default_bindings();

  nlohmann::json array = nlohmann::json::array();
  for (const nlohmann::json &row : serialize(defaults)) {
    if (row.at("action").get<std::string>() == "MoveUp")
      continue;
    array.push_back(row);
  }
  array.push_back({{"action", "Cancel"}, {"device", "keyboard"}, {"input", "Up"}});

  const auto parsed = parse_bindings(array, defaults);
  REQUIRE(parsed.has_value());

  const std::vector<PhysicalInput> move_up = inputs_for(*parsed, Action::MoveUp);
  CHECK(std::ranges::find(move_up, physical(Key::W)) != move_up.end());
  CHECK(std::ranges::find(move_up, physical(GamepadControl::LeftStickUp)) != move_up.end());
  CHECK(std::ranges::find(move_up, physical(GamepadControl::DpadUp)) != move_up.end());
  CHECK(std::ranges::find(move_up, physical(Key::Up)) == move_up.end());
}

TEST_CASE("bindings: parsing an older file restores a new action's shared keyboard default") {
  const Bindings defaults = default_bindings();

  // An older settings file predating Action::Menu: every default row except Menu's. Escape is
  // already bound to Cancel, but that is an intended shared default, so Menu must still get it.
  nlohmann::json array = nlohmann::json::array();
  for (const nlohmann::json &row : serialize(defaults)) {
    if (row.at("action").get<std::string>() != "Menu")
      array.push_back(row);
  }

  const auto parsed = parse_bindings(array, defaults);
  REQUIRE(parsed.has_value());
  const std::vector<PhysicalInput> menu = inputs_for(*parsed, Action::Menu);
  CHECK(std::ranges::find(menu, physical(Key::Escape)) != menu.end());
  CHECK(std::ranges::find(menu, physical(GamepadControl::Start)) != menu.end());
  // Cancel keeps Escape too.
  const std::vector<PhysicalInput> cancel = inputs_for(*parsed, Action::Cancel);
  CHECK(std::ranges::find(cancel, physical(Key::Escape)) != cancel.end());
}

TEST_CASE("bindings: parsing restores a shared default missing from a partially-populated action") {
  const Bindings defaults = default_bindings();

  // Simulates a file saved after Menu lost its keyboard row but kept Start.
  nlohmann::json array = nlohmann::json::array();
  for (const nlohmann::json &row : serialize(defaults)) {
    const bool is_menu_escape =
        row.at("action").get<std::string>() == "Menu" && row.at("input").get<std::string>() == "Escape";
    if (!is_menu_escape)
      array.push_back(row);
  }

  const auto parsed = parse_bindings(array, defaults);
  REQUIRE(parsed.has_value());
  const std::vector<PhysicalInput> menu = inputs_for(*parsed, Action::Menu);
  CHECK(std::ranges::find(menu, physical(Key::Escape)) != menu.end());
  CHECK(std::ranges::find(menu, physical(GamepadControl::Start)) != menu.end());
}

TEST_CASE("bindings: a deliberate partial unbind of a shared key persists") {
  const Bindings defaults = default_bindings();

  // The player rebound Cancel off Escape (now H) but left its other rows. Escape stays off Cancel.
  nlohmann::json array = nlohmann::json::array();
  for (const nlohmann::json &row : serialize(defaults)) {
    if (row.at("action").get<std::string>() == "Cancel")
      continue;
    array.push_back(row);
  }
  array.push_back({{"action", "Cancel"}, {"device", "keyboard"}, {"input", "H"}});
  array.push_back({{"action", "Cancel"}, {"device", "gamepad"}, {"input", "B"}});

  const auto parsed = parse_bindings(array, defaults);
  REQUIRE(parsed.has_value());
  const std::vector<PhysicalInput> cancel = inputs_for(*parsed, Action::Cancel);
  CHECK(std::ranges::find(cancel, physical(Key::Escape)) == cancel.end());
  CHECK(std::ranges::find(cancel, physical(Key::H)) != cancel.end());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): CHECK expands to branches.
TEST_CASE("bindings: parse errors name the offending entry") {
  const Bindings defaults = default_bindings();

  {
    const auto result = parse_bindings(nlohmann::json::object(), defaults);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("array") != std::string::npos);
  }
  {
    nlohmann::json array = nlohmann::json::array();
    array.push_back({{"action", "Jump"}, {"device", "keyboard"}, {"input", "A"}});
    const auto result = parse_bindings(array, defaults);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("unknown action") != std::string::npos);
  }
  {
    nlohmann::json array = nlohmann::json::array();
    array.push_back({{"action", "Cancel"}, {"device", "joystick"}, {"input", "A"}});
    const auto result = parse_bindings(array, defaults);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("unknown device") != std::string::npos);
  }
  {
    nlohmann::json array = nlohmann::json::array();
    array.push_back({{"action", "Cancel"}, {"device", "keyboard"}, {"input", "LeftStickUp"}});
    const auto result = parse_bindings(array, defaults);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("unknown input") != std::string::npos);
  }
  {
    nlohmann::json rows = nlohmann::json::array();
    std::size_t made{};
    for (std::uint16_t code = 0; code <= 400 && made < k_max_input_sources + 1; ++code) {
      const auto input = physical(static_cast<Key>(code));
      const std::string_view name = name_of(input);
      if (name.empty())
        continue;
      const auto action = static_cast<Action>(made % k_action_count);
      rows.push_back({
          {"action", std::string(action_name(action))},
          {"device", std::string(device_name(InputDevice::Keyboard))},
          {"input", std::string(name)},
      });
      ++made;
    }
    const auto result = parse_bindings(rows, defaults);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find(std::to_string(k_max_input_sources)) != std::string::npos);
  }
}

TEST_CASE("bindings: Cancel includes the right mouse button") {
  const Bindings defaults = default_bindings();
  const std::vector<PhysicalInput> cancel = inputs_for(defaults, Action::Cancel);
  CHECK(std::ranges::find(cancel, physical(corundum::input::MouseButton::Right)) != cancel.end());
  CHECK(std::ranges::find(cancel, physical(Key::Escape)) != cancel.end());
  CHECK(std::ranges::find(cancel, physical(GamepadControl::B)) != cancel.end());
}
