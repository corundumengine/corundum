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
  /// toward the current version, returning the version it reached. Steps run in
  /// ascending order and must never be edited once shipped, since already-migrated
  /// files depend on the exact transformation a step performed.
  ///
  /// @param root The parsed asset document to rewrite.
  /// @param from_version The schema version @p root currently declares.
  /// @param path Source file path, for error messages.
  /// @return The version @p root now conforms to (normally the current version), or a
  ///         message describing the failure. Returning a version below the requested
  ///         current version is treated as an incomplete migration and rejected.
  using SchemaMigration =
      std::function<std::expected<int, std::string>(nlohmann::json &root, int from_version, const std::string &path)>;

  /// Parse, gate, and migrate an asset document's `schema_version` field.
  ///
  /// Reads the optional `schema_version` field (absent -> legacy version 1), rejects
  /// a version above @p current_version or below 1, and runs @p migrate when the
  /// document predates @p current_version. The migration must report — and @p root is
  /// checked to confirm — that it reached @p current_version, so a no-op or
  /// prematurely-stopped migration fails loudly instead of letting an old document be
  /// parsed as current. Call this before schema validation so a migration can bring an
  /// older document into the current shape first.
  ///
  /// @param root The parsed asset document; migrated in place and stamped with
  ///             @p current_version when a migration runs.
  /// @param current_version The engine's current schema version for this asset kind.
  /// @param asset_label Asset kind for error messages (e.g. "Quest").
  /// @param path Source file path, for error messages.
  /// @param migrate The migration step, or an empty function when none exists yet.
  ///                An empty function is accepted only for a document already at
  ///                @p current_version.
  /// @return ok on success (with @p root at @p current_version), or a message
  ///         describing the failure.
  [[nodiscard]] std::expected<void, std::string> prepare_schema_version(nlohmann::json &root, int current_version,
                                                                        std::string_view asset_label,
                                                                        const std::string &path,
                                                                        const SchemaMigration &migrate);

} // namespace corundum::core
