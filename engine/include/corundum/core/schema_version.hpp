// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <expected>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>

namespace corundum::core {

  /// A migration step that rewrites an asset document in place from @p from_version
  /// toward the current version. Steps run in ascending order and must never be
  /// edited once shipped, since already-migrated files depend on the exact
  /// transformation a step performed.
  ///
  /// @param root The parsed asset document to rewrite.
  /// @param from_version The schema version @p root currently declares.
  /// @param path Source file path, for error messages.
  /// @return ok on success, or a message describing the failure.
  using SchemaMigration =
      std::function<std::expected<void, std::string>(nlohmann::json &root, int from_version, const std::string &path)>;

  /// Parse, gate, and migrate an asset document's `schema_version` field.
  ///
  /// Reads the optional `schema_version` field (absent -> legacy version 1), rejects
  /// a version above @p current_version or below 1, and runs @p migrate when the
  /// document predates @p current_version. Call this before schema validation so a
  /// migration can bring an older document into the current shape first.
  ///
  /// @param root The parsed asset document; migrated in place.
  /// @param current_version The engine's current schema version for this asset kind.
  /// @param asset_label Asset kind for error messages (e.g. "Quest").
  /// @param path Source file path, for error messages.
  /// @param migrate The migration step, or an empty function when none exists yet.
  /// @return ok on success (with @p root at @p current_version), or a message
  ///         describing the failure.
  [[nodiscard]] std::expected<void, std::string> prepare_schema_version(nlohmann::json &root, int current_version,
                                                                        std::string_view asset_label,
                                                                        const std::string &path,
                                                                        const SchemaMigration &migrate);

} // namespace corundum::core
