// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/ui/font_family.hpp>

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::ui {

  /// A span of text sharing one font style, produced by parse_styled. Adjacent runs never
  /// carry the same style.
  struct StyledRun {
    std::string text{};

    FontStyle style{FontStyle::Regular};

    bool operator==(const StyledRun &) const = default;
  };

  /// A run positioned within a wrapped line. @c x is the cumulative offset from the line
  /// origin, so callers draw each segment at @c line_origin.x + x.
  struct StyledSegment {
    std::string text{};

    FontStyle style{FontStyle::Regular};

    float x{0.f};

    bool operator==(const StyledSegment &) const = default;
  };

  /// Parses inline markdown-lite markup into styled runs:
  /// `*italic*`, `**bold**`, `***bold-italic***`, `\*` for a literal asterisk and `\\` for a
  /// literal backslash. Delimiters toggle the bold/italic bits on, then off at the matching
  /// delimiter of the same length; a delimiter with no matching close is emitted as literal
  /// text. A backslash before any other character (for example `\n`) stays literal. "\r\n" and
  /// lone '\r' are normalized to '\n' so styled text wraps like plain text.
  /// @param text UTF-8 source. Asterisks and backslashes are ASCII, so parsing operates on bytes
  ///             and never splits a multibyte sequence.
  /// @return The styled runs; empty for empty input.
  [[nodiscard]] std::vector<StyledRun> parse_styled(std::string_view text);

  /// Returns the longest prefix of @p segments that fits @p budget visible codepoints, and how
  /// many codepoints that prefix consumed. A truncation mid-segment keeps that segment's style
  /// and x offset. Used by the typewriter reveal, which reveals by codepoint across style
  /// boundaries.
  /// @pre @p budget may be zero or negative, in which case the result is empty.
  [[nodiscard]] std::pair<std::vector<StyledSegment>, int> reveal_prefix_styled(std::span<const StyledSegment> segments,
                                                                                int budget);

} // namespace corundum::ui
