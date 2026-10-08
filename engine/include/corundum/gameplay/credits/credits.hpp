// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

namespace corundum::gameplay::credits {

  /** @brief One credits block: a heading followed by its lines.
   *
   *  Loaded from the JSON file named by ResourcePaths::credits_file. Passive content, like a
   *  codex entry: the Credits screen owns presentation, this only holds the text. */
  struct CreditsSection {
    std::string heading{}; ///< Section heading, e.g. "Design"; shown above its lines.

    std::vector<std::string> lines{}; ///< Body lines, drawn in order beneath the heading.
  };

} // namespace corundum::gameplay::credits
