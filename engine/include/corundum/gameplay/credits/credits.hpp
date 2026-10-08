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

  /** @brief A whole credits file: an optional title and background, plus its sections.
   *
   *  The title is drawn large above the first section; sections carry the smaller gold headings.
   *  The background is an optional image drawn behind the whole roll. */
  struct CreditsFile {
    std::string title{};

    std::string background{}; ///< Path to a background image, empty for none.

    std::vector<CreditsSection> sections{};
  };

} // namespace corundum::gameplay::credits
