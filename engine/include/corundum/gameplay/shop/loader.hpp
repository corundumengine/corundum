// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/shop/shop.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::core {
class ContentSource;
}

namespace corundum::gameplay::shop {

/** Loads shop batch files, validating each against the shop schema compiled once at construction. */
class Loader {
public:
  Loader();

  /** Load one shop batch file (`data/shops/<file>.json`).
   *
   *  Expects an object with a `schema_version` and a `shops` array. The whole document is
   *  validated against the shop schema, so one malformed shop fails the file.
   *
   *  @param path Batch file to load.
   *  @return The loaded shops, or an error describing an open, version, or format failure.
   */
  [[nodiscard]] std::expected<std::vector<Shop>, std::string> load(const core::ContentSource &content,
                                                                   const std::filesystem::path &path) const;

private:
  core::SchemaValidator schema_;
};

} // namespace corundum::gameplay::shop
