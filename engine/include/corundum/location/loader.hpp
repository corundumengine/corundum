// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/location/location.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::location {

  /** @brief Load one location batch file (`data/locations/<file>.json`).
   *
   *  Expects an object with a `schema_version` and a `locations` array. Each entry is
   *  validated against the location schema; an invalid entry is skipped with a warning.
   *
   *  @param path Batch file to load.
   *  @return The loaded locations, or an error describing an open, version, or format failure.
   */
  [[nodiscard]] std::expected<std::vector<Location>, std::string> load_location_file(const std::filesystem::path &path);

} // namespace corundum::location
