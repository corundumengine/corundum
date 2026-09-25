// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace corundum::core {

  /// Write a JSON value to @p path with sorted keys and 2-space indentation.
  /// Produces stable diffs — reordering fields in source code won't change output.
  /// Writes to "<path>.tmp" and renames it over @p path, so a failed write never truncates an existing file.
  /// @returns ok on success, or an error message if the file could not be opened or written.
  [[nodiscard]] std::expected<void, std::string> write_json(const std::filesystem::path &path, const nlohmann::json &j);

  /// Read and parse a JSON file at @p path.
  /// Catches parse errors internally and returns them via std::expected.
  /// @param label Noun naming the document for error messages (e.g. "item JSON"). When empty,
  ///              errors read "cannot open: <path>" / "malformed JSON in <path>: <detail>".
  /// @note `//` and `/* */` comments are permitted and ignored. Comments are
  ///       not preserved: write_json() emits plain JSON, so any tool that
  ///       round-trips a file drops hand-written comments.
  /// @returns The parsed JSON on success, or an error message on failure.
  [[nodiscard]] std::expected<nlohmann::json, std::string> read_json(const std::filesystem::path &path,
                                                                     std::string_view label = {});

} // namespace corundum::core
