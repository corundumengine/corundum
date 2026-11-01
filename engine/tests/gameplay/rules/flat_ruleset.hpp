// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/gameplay/rules/rules.hpp>

#include <format>
#include <vector>

namespace corundum::test {

  /// The minimal ruleset the framework is tested against: one Health row read verbatim from the
  /// `test.health` and `test.max_health` flags. It holds no game-specific rule.
  inline gameplay::rules::Ruleset make_flat_ruleset() {
    return gameplay::rules::Ruleset{
        .character_rows =
            [](const gameplay::rules::FlagReader &reader) {
              return std::vector<gameplay::rules::StatRow>{
                  {
                      .label = "Health",
                      .value = std::format("{} / {}", reader.read("test.health"), reader.read("test.max_health")),
                  },
              };
            },
    };
  }

} // namespace corundum::test
