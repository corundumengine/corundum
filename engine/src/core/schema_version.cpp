// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/schema_version.hpp>
#include <expected>
#include <format>
// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is
// still required to read and rewrite json values (the forward header is incomplete).
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::core {

  namespace {

    /// Parse the optional "schema_version" field. Absent -> legacy version 1.
    std::expected<int, std::string> parse_schema_version(const nlohmann::json &root, std::string_view asset_label,
                                                         const std::string &path) {
      if (!root.contains("schema_version"))
        return 1;
      if (!root["schema_version"].is_number_integer())
        return std::unexpected(std::format("{} '{}' field 'schema_version' must be an integer", asset_label, path));
      return root["schema_version"].get<int>();
    }

  } // namespace

  std::expected<void, std::string> prepare_schema_version(nlohmann::json &root, int current_version,
                                                          std::string_view asset_label, const std::string &path,
                                                          const SchemaMigration &migrate) {
    auto parsed = parse_schema_version(root, asset_label, path);
    if (!parsed)
      return std::unexpected(std::move(parsed).error());
    const int schema_version = *parsed;

    if (schema_version > current_version)
      return std::unexpected(std::format("{} '{}' has schema_version {}, newer than this engine supports (max {}) — "
                                         "update the engine",
                                         asset_label, path, schema_version, current_version));

    if (schema_version < 1)
      return std::unexpected(std::format("{} '{}' has invalid schema_version {}", asset_label, path, schema_version));

    if (schema_version < current_version && migrate)
      return migrate(root, schema_version, path);

    return {};
  }

} // namespace corundum::core
