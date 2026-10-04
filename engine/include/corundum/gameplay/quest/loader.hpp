// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/quest/quest.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::gameplay::quest {

  /**
   * @brief Load and validate a quest from a JSON file.
   *
   * Rejects the file if its schema version is not the current one, if it
   * fails schema validation, or if it violates a `quest::validate` invariant (see that
   * function's contract). Non-fatal diagnostics — a `type` field other than
   * "quest", or a stage list whose order disagrees with its sequence integers
   * — are printed to stderr and do not fail the load.
   *
   * @param path Filesystem path to the quest JSON file.
   * @return The parsed Quest on success, or a message describing the failure.
   */
  [[nodiscard]] std::expected<Quest, std::string> load_quest(const std::filesystem::path &path);

} // namespace corundum::gameplay::quest
