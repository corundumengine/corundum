// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/gameplay/shop/shop.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace corundum::gameplay::shop {

  /** @brief Load one shop batch file (`data/shops/<file>.json`).
   *
   *  Expects an object with a `schema_version` and a `shops` array. Each shop is validated
   *  against the shop schema; an invalid shop is skipped with a warning.
   *
   *  @param path Batch file to load.
   *  @return The loaded shops, or an error describing an open, version, or format failure.
   */
  [[nodiscard]] std::expected<std::vector<Shop>, std::string> load_shop_file(const std::filesystem::path &path);

} // namespace corundum::gameplay::shop
