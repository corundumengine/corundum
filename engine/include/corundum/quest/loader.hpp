// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/quest/quest.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::quest {

  /**
   * @brief Load and validate a quest from a JSON file.
   *
   * Rejects a file that fails schema validation or breaks an in-memory quest
   * invariant: duplicate stage names or sequences, no resolved stage, or an
   * `advances_to` / `auto_advance_to` target naming no stage. Non-fatal
   * diagnostics — a `type` field other than "quest", or a stage list whose
   * order disagrees with its sequence integers — are printed to stderr and do
   * not fail the load.
   *
   * @param path Filesystem path to the quest JSON file.
   * @return The parsed Quest on success, or a message describing the failure.
   */
  [[nodiscard]] std::expected<Quest, std::string> load_quest(const std::filesystem::path &path);

} // namespace corundum::quest
