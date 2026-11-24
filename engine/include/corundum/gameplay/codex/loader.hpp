// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/codex/codex.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::core {
class ContentSource;
}

namespace corundum::gameplay::codex {

/** Loads codex batch files, validating each against the codex schema compiled once at construction. */
class Loader {
public:
  Loader();

  /** Load one codex batch file (`data/codex/<file>.json`).
   *
   *  Expects an object with a `schema_version` and an `entries` array. The whole document
   *  is validated against the codex schema, so one malformed entry fails the file.
   *
   *  @param path Batch file to load.
   *  @return The loaded entries, or an error describing an open, version, or format failure.
   */
  [[nodiscard]] std::expected<std::vector<CodexEntry>, std::string> load(const core::ContentSource &content,
                                                                         const std::filesystem::path &path) const;

private:
  core::SchemaValidator schema_;
};

} // namespace corundum::gameplay::codex
