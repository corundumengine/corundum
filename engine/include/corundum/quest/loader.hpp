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
   * Migrates the file to the current schema version first, then rejects it if
   * the version is newer than the engine supports, if it fails schema
   * validation, or if it breaks a quest invariant: an empty quest id or name,
   * an empty or non-compiling `done_condition`, duplicate stage names or
   * sequences, no resolved stage, or an `advances_to` / `auto_advance_to`
   * target naming no stage. Non-fatal diagnostics — a `type` field other than
   * "quest", or a stage list whose order disagrees with its sequence integers
   * — are printed to stderr and do not fail the load.
   *
   * @param path Filesystem path to the quest JSON file.
   * @return The parsed Quest on success, or a message describing the failure.
   */
  [[nodiscard]] std::expected<Quest, std::string> load_quest(const std::filesystem::path &path);

} // namespace corundum::quest
