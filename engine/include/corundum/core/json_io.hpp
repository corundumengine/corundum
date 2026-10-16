// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <expected>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>

namespace corundum::core {

  class ContentSource;

  /// Serialise @p j as write_json() does: sorted keys, 2-space indentation, trailing newline.
  [[nodiscard]] std::string format_json(const nlohmann::json &j);

  /// Write @p contents to "<path>.tmp" and rename it over @p path, so a failed write never truncates an
  /// existing file. Does not create parent directories.
  /// @returns ok on success, or an error message naming the path.
  [[nodiscard]] std::expected<void, std::string> write_file_atomic(const std::filesystem::path &path,
                                                                   std::string_view contents);

  /// Parse JSON text (comments allowed). @p source names the document in error messages.
  /// @param label Noun naming the document for error messages (e.g. "item JSON"); empty reads "JSON".
  [[nodiscard]] std::expected<nlohmann::json, std::string> parse_json(std::string_view text, std::string_view source,
                                                                      std::string_view label = {});

  /// Parse JSON bytes (comments allowed); same errors as the text overload.
  /// @param label Noun naming the document for error messages (e.g. "item JSON"); empty reads "JSON".
  [[nodiscard]] std::expected<nlohmann::json, std::string>
  parse_json(std::span<const std::uint8_t> bytes, std::string_view source, std::string_view label = {});

  /// Read @p path through @p content and parse it.
  /// @param label Noun naming the document for error messages. A read failure returns the content
  ///              source's own error, prefixed "<label>: " when a label is given (e.g. "item JSON: cannot
  ///              open: data/items/x.json"). A parse failure reads "malformed <label or JSON> in <path>: <detail>".
  /// @note `//` and `/* */` comments are permitted and ignored, and are not preserved by write_json().
  [[nodiscard]] std::expected<nlohmann::json, std::string>
  read_json(const ContentSource &content, const std::filesystem::path &path, std::string_view label = {});

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
