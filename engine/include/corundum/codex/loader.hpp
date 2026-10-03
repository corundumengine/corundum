// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/codex/codex.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::codex {

  /** @brief Load one codex batch file (`data/codex/<file>.json`).
   *
   *  Expects an object with a `schema_version` and an `entries` array. Each entry is
   *  validated against the codex schema; an invalid entry is skipped with a warning
   *  rather than failing the whole file.
   *
   *  @param path Batch file to load.
   *  @return The loaded entries, or an error describing an open, version, or format failure.
   */
  [[nodiscard]] std::expected<std::vector<CodexEntry>, std::string> load_codex_file(const std::filesystem::path &path);

} // namespace corundum::codex
