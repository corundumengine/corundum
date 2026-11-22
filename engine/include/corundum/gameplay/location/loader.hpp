// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/location/location.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::core {
  class ContentSource;
}

namespace corundum::gameplay::location {

  /** @brief Loads location batch files, validating each against the location schema compiled once at construction. */
  class Loader {
  public:
    Loader();

    /** @brief Load one location batch file (`data/locations/<file>.json`).
     *
     *  Expects an object with a `schema_version` and a `locations` array. The whole document
     *  is validated against the location schema, so one malformed entry fails the file.
     *
     *  @param path Batch file to load.
     *  @return The loaded locations, or an error describing an open, version, or format failure.
     */
    [[nodiscard]] std::expected<std::vector<Location>, std::string> load(const core::ContentSource &content,
                                                                         const std::filesystem::path &path) const;

  private:
    core::SchemaValidator schema_;
  };

} // namespace corundum::gameplay::location
