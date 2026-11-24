// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>

#include <nlohmann/json_fwd.hpp>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::input {

/** One physical input driving one Action. */
struct Binding {
  Action action{};

  PhysicalInput input{};

  friend bool operator==(const Binding &, const Binding &) = default;
};

/** Ordered binding table.
 *
 *  Row order is display order among an action's inputs. One input may appear in several rows and
 *  then drives every action it is bound to; bind(), rebind() and bind_swapping() each leave the
 *  input they bind on one action only, and the Controls menu uses bind_swapping().
 */
using Bindings = std::vector<Binding>;

/** The engine's default desktop bindings. */
[[nodiscard]] Bindings default_bindings();

/** Stable JSON name of `action`: its enumerator spelling ("MoveUp", "ZoomIn"). */
[[nodiscard]] std::string_view action_name(Action action) noexcept;

/** Inverse of action_name(); nullopt for an unknown name or "Count". */
[[nodiscard]] std::optional<Action> parse_action(std::string_view name) noexcept;

/** Inputs bound to `action`, in row order. */
[[nodiscard]] std::vector<PhysicalInput> inputs_for(const Bindings &bindings, Action action);

/** Bind `input` to `action` as a new last row.
 *
 *  @post `input` drives only `action`: every other row holding it is removed first.
 */
void bind(Bindings &bindings, Action action, PhysicalInput input);

/** Replace `from` with `to` in `action`'s row, keeping the row's position.
 *
 *  Appends a row instead when `action` has no `from` row.
 *
 *  @post `to` drives only `action`.
 */
void rebind(Bindings &bindings, Action action, PhysicalInput from, PhysicalInput to);

/** Remove the row binding `input` to `action`, if any. */
void unbind(Bindings &bindings, Action action, PhysicalInput input);

/** Make `input` `action`'s only binding on its device, swapping it out of other actions.
 *
 *  Removes all of `action`'s other rows on `input`'s device (a Controls rebind replaces the
 *  action's binding on that device) and every other action's row holding
 *  `input`. Each action left with no binding on that device then gets `action`'s first other
 *  input on that device in row order, so a rebind never strands the action it takes the input
 *  from: rebinding Cancel from gamepad B to A gives Select and Activate B. `action`'s further
 *  inputs on that device (MoveUp's Up when it also had W) are dropped, not redistributed. When
 *  `action` had no other input on that device, the losing actions are left without one.
 *
 *  @post `input` drives only `action`, and `action` has no other row on `input`'s device.
 */
void bind_swapping(Bindings &bindings, Action action, PhysicalInput input);

/** JSON array of {"action", "device", "input"} objects, in row order. */
[[nodiscard]] nlohmann::json serialize(const Bindings &bindings);

/** Parse a serialize() array, filling gaps from `defaults`.
 *
 *  A missing default row is restored so a newly added Action works with an older file. A plain
 *  default is only restored for an action with no rows at all, so a deliberate partial unbind
 *  survives a load. An input the defaults themselves share across actions (Escape = Cancel +
 *  Menu; Enter/Space/left mouse/A = Select + Activate) is restored for any device the action has
 *  no bindings on, since that overlap is intentional rather than a conflict, unless the file
 *  binds that input to an action the defaults don't give it to.
 *
 *  The consequence is that "this action has no inputs" cannot be persisted: an action the player
 *  fully unbound comes back with its defaults on the next load. A controls menu should therefore
 *  forbid emptying an action, or accept this behaviour. Exact duplicate rows collapse to one.
 *
 *  @return The table, or an error naming the offending entry's index when `array` is not an
 *          array, an entry is malformed, or its action, device or input name is unknown. Also an
 *          error when the result exceeds k_max_input_sources rows.
 */
[[nodiscard]] std::expected<Bindings, std::string> parse_bindings(const nlohmann::json &array,
                                                                  const Bindings &defaults);

} // namespace corundum::input
