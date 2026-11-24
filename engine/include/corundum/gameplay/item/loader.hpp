// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/item/item.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::core {
class ContentSource;
}

namespace corundum::gameplay::item {

/** @brief Loads item batch files, validating each item against the item schema compiled once at construction. */
class Loader {
public:
  Loader();

  /**
   * @brief Load and validate a batch item file.
   *
   * A batch file holds multiple items as a JSON array under "items", wrapped
   * with a "schema_version" — same shape as atlas clips sidecars. Each element
   * is validated against the per-item JSON schema; the folder-derived category
   * is injected into each element before validation and overwrites any
   * conflicting "category" the author wrote.
   *
   * @param path Filesystem path to the item batch JSON file.
   * @param category Category of the folder this file lives in; injected into
   *                 every item in the file.
   * @return The parsed Items on success, or an error message on failure.
   */
  [[nodiscard]] std::expected<std::vector<Item>, std::string>
  load(const core::ContentSource &content, const std::filesystem::path &path, ItemCategory category) const;

private:
  core::SchemaValidator schema_;
};

} // namespace corundum::gameplay::item
