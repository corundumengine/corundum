// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The seam between the gameplay framework and a game's rules. The framework decides when a value
// is needed and how it is shown; the ruleset decides what it is. A ruleset keeps its state in the
// playthrough's flags under its own key prefix, so saves, loads and playthrough boundaries carry it
// with no hook of their own; it reads that state through FlagReader, never the FlagStore itself.
// This header is part of corundum::rules_interface and includes no other corundum header, so a
// ruleset builds against that target alone.
namespace corundum::gameplay::rules {

  /** @brief One "label: value" row a ruleset supplies for display, such as {"Strength", "+2"}. */
  struct StatRow {
    std::string label{};

    std::string value{};

    bool operator==(const StatRow &) const = default;
  };

  /** @brief Reads the playthrough's integer state by FlagStore key; an absent key reads as 0.
   *  @note Valid only for the duration of the ruleset call it is passed to; never store it. */
  struct FlagReader {
    std::function<int(std::string_view key)> read{};
  };

  /** @brief A game's rules, as callbacks the framework calls when it needs a value.
   *
   *  A struct of std::function rather than a virtual interface, matching the framework's other
   *  game hooks (Gameplay::on_event). A callback may be empty, and the framework then shows
   *  nothing for it. A callback reads only what it is passed.
   */
  struct Ruleset {
    /** @brief The protagonist's rows for the character sheet's Stats column, in display order. */
    std::function<std::vector<StatRow>(const FlagReader &)> character_rows{};
  };

} // namespace corundum::gameplay::rules
