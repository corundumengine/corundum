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

  /** @brief One physical input driving one Action. */
  struct Binding {
    Action action{};

    PhysicalInput input{};

    friend bool operator==(const Binding &, const Binding &) = default;
  };

  /** @brief Ordered binding table.
   *
   *  Row order is display order among an action's inputs. One input may appear in several rows and
   *  then drives every action it is bound to; bind() and rebind() are what keep a player's edits
   *  conflict-free.
   */
  using Bindings = std::vector<Binding>;

  /** @brief The engine's default desktop bindings. */
  [[nodiscard]] Bindings default_bindings();

  /** @brief Stable JSON name of @p action: its enumerator spelling ("MoveUp", "ZoomIn"). */
  [[nodiscard]] std::string_view action_name(Action action) noexcept;

  /** @brief Inverse of action_name(); nullopt for an unknown name or "Count". */
  [[nodiscard]] std::optional<Action> parse_action(std::string_view name) noexcept;

  /** @brief Inputs bound to @p action, in row order. */
  [[nodiscard]] std::vector<PhysicalInput> inputs_for(const Bindings &bindings, Action action);

  /** @brief Bind @p input to @p action as a new last row.
   *
   *  @post @p input drives only @p action: every other row holding it is removed first.
   */
  void bind(Bindings &bindings, Action action, PhysicalInput input);

  /** @brief Replace @p from with @p to in @p action's row, keeping the row's position.
   *
   *  Appends a row instead when @p action has no @p from row.
   *
   *  @post @p to drives only @p action.
   */
  void rebind(Bindings &bindings, Action action, PhysicalInput from, PhysicalInput to);

  /** @brief Remove the row binding @p input to @p action, if any. */
  void unbind(Bindings &bindings, Action action, PhysicalInput input);

  /** @brief JSON array of {"action", "device", "input"} objects, in row order. */
  [[nodiscard]] nlohmann::json serialize(const Bindings &bindings);

  /** @brief Parse a serialize() array, filling gaps from @p defaults.
   *
   *  Any action with no row in @p array receives its @p defaults rows, except those whose input is
   *  already bound, so a newly added Action works with an older file. The consequence is that
   *  "this action has no inputs" cannot be persisted: an action the player fully unbound comes back
   *  with its defaults on the next load. Partial unbinds persist. A controls menu should therefore
   *  forbid emptying an action, or accept this behaviour. Exact duplicate rows collapse to one.
   *
   *  @return The table, or an error naming the offending entry's index when @p array is not an
   *          array, an entry is malformed, or its action, device or input name is unknown. Also an
   *          error when the result exceeds k_max_input_sources rows.
   */
  [[nodiscard]] std::expected<Bindings, std::string> parse_bindings(const nlohmann::json &array,
                                                                    const Bindings &defaults);

} // namespace corundum::input
